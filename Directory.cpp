#include "Directory.h"
#include "Util.h"

#include <cassert>
#include <iostream>

using namespace std;

const int PAGE_SHIFT = 12;
const int PAGE_SIZE = (1 << PAGE_SHIFT);

mutex Directory::_dirMutex;
mutex DirectorySet::_dsMutex;

Directory::Directory( unsigned int lineSize )
 : _addrShift(floorLog2(lineSize))
{
}

CacheState Directory::request( Cache* cache, 
                               uintptr_t addr, 
                               CacheState reqState, 
                               bool* safe )
{
   lock_guard<mutex> lock( _dirMutex );

   DirectoryEntry& dirEntry = _dir[addr >> _addrShift];

   if( dirEntry.modified )
      assert( dirEntry.caches.size() == 1 );

   if( dirEntry.owner == nullptr )
   {
      dirEntry.owner = cache;
      dirEntry.readOnly = reqState < Modified;
   }
   else
   {
      dirEntry.shared   = (dirEntry.owner != cache);
      dirEntry.readOnly = dirEntry.readOnly && (reqState < Modified);
   }

   bool isSafe = !dirEntry.shared || dirEntry.readOnly;
   if( safe != nullptr )
      *safe = isSafe;

   switch( reqState )
   {
   case Shared:
      if( !dirEntry.modified )
      {
         // A single cache may have an Exclusive copy
         if( dirEntry.caches.size() == 1 )
            dirEntry.caches.front()->downgrade( addr, Shared, isSafe );
      }
      else
      {
         dirEntry.caches.front()->downgrade( addr, Shared, isSafe );
         dirEntry.modified = false;
      }

      dirEntry.caches.push_back( cache );

      if( dirEntry.caches.size() == 1 )
         return Exclusive;
      else
         return Shared;
      break;

   case Exclusive:
   case Modified:
      if( !dirEntry.caches.empty() )
      {
         for( auto it = dirEntry.caches.begin(); it != dirEntry.caches.end(); ++it )
         {
            if( *it != cache )
               (*it)->downgrade( addr, Invalid, isSafe );
         }
         dirEntry.caches.clear();
      }
      dirEntry.caches.push_back( cache );
      dirEntry.modified = (reqState == Modified);
      return reqState;
      break;

   // Invalid indicates a writeback/eviction
   case Invalid:
      if( dirEntry.modified )
      {
         dirEntry.modified = false;
         dirEntry.caches.clear();
      }
      else
      {
         for( auto it = dirEntry.caches.begin(); it != dirEntry.caches.end(); ++it )
         {
            if( *it == cache )
            {
               dirEntry.caches.erase( it );
               break;
            }
         }
      }
      return Invalid;
      break;

   default:
      cerr << "Request for unknown state" << endl;
      break;
   }

   return Invalid;
}

DirectorySet::DirectorySet( unsigned int numSites, unsigned int lineSize )
{
   for( unsigned int i = 0; i < numSites; ++i )
   {
      _sites.push_back( new Directory(lineSize) );
   }
}

Directory& DirectorySet::find( uintptr_t addr )
{
   lock_guard<mutex> lock( _dsMutex );

   uintptr_t vpn = addr >> PAGE_SHIFT;
   unsigned int ppn;

   map<uintptr_t,unsigned int>::const_iterator it = _pageMap.find( vpn );

   if( it != _pageMap.end() )
      ppn = it->second;
   else
   {
      ppn = _pageMap.size();
      _pageMap[vpn] = ppn;
   }

   int siteId = ppn % _sites.size();

   _dsMutex.unlock();

   return *_sites[siteId];
}
