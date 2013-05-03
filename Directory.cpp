#include "Directory.h"
#include "Util.h"

#include <cassert>
#include <iostream>

using namespace std;

const int PAGE_SHIFT = 12;
const int PAGE_SIZE = (1 << PAGE_SHIFT);

Directory::Directory( unsigned int lineSize )
 : _addrShift(floorLog2(lineSize)),
   _allowReverseTransition(false)
{
}

CacheState Directory::request( Cache* cache, 
                               uintptr_t addr, 
                               CacheState reqState, 
                               bool* safe )
{
   // Find entry, optionally creating a new one
   DirectoryEntry& dirEntry = _dir[addr >> _addrShift];

   if( dirEntry.modified )
      assert( dirEntry.caches.size() == 1 );

   // Update safety state of directory
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

   // Reduce state down to one bit
   bool isSafe = !dirEntry.shared || dirEntry.readOnly;
   if( safe != nullptr )
      *safe = isSafe;

   switch( reqState )
   {
   case Shared:
      if( dirEntry.caches.size() == 1 )
         dirEntry.caches.front()->downgrade( addr, Shared, isSafe );

      dirEntry.modified = false;

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

      // Transition back to safe if no caches have a copy anymore
      if( _allowReverseTransition && dirEntry.caches.empty() )
      {
         dirEntry.owner = nullptr;
         dirEntry.shared = false;
         dirEntry.readOnly = true;
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
   uintptr_t vpn = addr >> PAGE_SHIFT;
   unsigned int ppn;

   auto it = _pageMap.find( vpn );

   if( it != _pageMap.end() )
      ppn = it->second;
   else
   {
      ppn = _pageMap.size();
      _pageMap[vpn] = ppn;
   }

   int siteId = ppn % _sites.size();

   return *_sites[siteId];
}

void DirectorySet::setAllowReverseTransition( bool allow )
{
   for( auto it = _sites.begin(); it != _sites.end(); ++it )
   {
      (*it)->_allowReverseTransition = allow;
   }
}

void DirectorySet::printStats( ostream& stream ) const
{
   int numLinesTotal = 0;
   int untouchedTotal = 0;
   int p_roTotal = 0;
   int p_rwTotal = 0;
   int s_roTotal = 0;
   int s_rwTotal = 0;

   for( unsigned int i = 0; i < _sites.size(); ++i )
   {
      stream << "Site " << i << endl;

      int numLines = _sites[i]->_dir.size();
      int untouched = 0;
      int p_ro = 0;
      int p_rw = 0;
      int s_ro = 0;
      int s_rw = 0;

      for( auto it = _sites[i]->_dir.begin(); it != _sites[i]->_dir.end(); ++it )
      {
         auto& entry = it->second;
         if( entry.owner == nullptr )
            ++untouched;
         else if( !entry.shared )
         {
            if( entry.readOnly )
               ++p_ro;
            else
               ++p_rw;
         }
         else
         {
            if( entry.readOnly )
               ++s_ro;
            else
               ++s_rw;
         }
      }

      stream << numLines << " Total lines accessed"
             << " (" << 100.0*untouched/numLines << "% Untouched)"
             << " (" << 100.0*p_ro/numLines << "% P_RO)"
             << " (" << 100.0*p_rw/numLines << "% P_RW)"
             << " (" << 100.0*s_ro/numLines << "% S_RO)"
             << " (" << 100.0*s_rw/numLines << "% S_RW)"
             << endl;

      numLinesTotal += numLines;
      untouchedTotal += untouched;
      p_roTotal += p_ro;
      p_rwTotal += p_rw;
      s_roTotal += s_ro;
      s_rwTotal += s_rw;
   }

   stream << "All Sites" << endl;
   stream << numLinesTotal << " Total lines accessed"
          << " (" << 100.0*untouchedTotal/numLinesTotal << "% Untouched)"
          << " (" << 100.0*p_roTotal/numLinesTotal << "% P_RO)"
          << " (" << 100.0*p_rwTotal/numLinesTotal << "% P_RW)"
          << " (" << 100.0*s_roTotal/numLinesTotal << "% S_RO)"
          << " (" << 100.0*s_rwTotal/numLinesTotal << "% S_RW)"
          << endl;
}
