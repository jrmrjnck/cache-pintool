#include "Cache.h"
#include "Util.h"
#include "Directory.h"

#include <cassert>
#include <iostream>

using namespace std;

Cache::Cache( size_t cacheSize, size_t lineSize, unsigned int assoc )
{
   assert( cacheSize != 0 );
   assert( lineSize != 0 );
   assert( isPowerOf2(lineSize) );
   assert( assoc != 0 );
   assert( cacheSize % (lineSize*assoc) == 0 );

   _sets     = cacheSize / (lineSize*assoc);
   _lineSize = lineSize;
   _assoc    = assoc;

   _offsetMask = _lineSize - 1;
   _setShift   = floorLog2( _lineSize );
   _setMask    = (_sets - 1) << _setShift;
   _tagShift   = _setShift + floorLog2(_sets);
   _tagMask    = ~(_setMask | _offsetMask);

   _lines = new CacheLine*[_sets];
   for( unsigned int s = 0; s < _sets; ++s )
   {
      _lines[s] = new CacheLine[_assoc]();
   }
}

Cache::~Cache()
{
   for( unsigned int s = 0; s < _sets;  ++s )
   {
      delete [] _lines[s];
   }

   delete [] _lines;
}

bool Cache::access( AccessType type, uintptr_t addr, size_t length )
{
   // Check for hit
   bool hit = false;
   bool partialHit = false;

   unsigned int set = (addr & _setMask) >> _setShift;
   uintptr_t tag    = (addr & _tagMask) >> _tagShift;

   CacheLine* targetLine = _find( set, tag );
   if( targetLine != NULL )
   {
      if( type == Store && targetLine->state < Modified )
         partialHit = true;
      else
         hit = true;
   }

   if( hit )
      _updateLru( set, targetLine );
   else
   {
      Directory& dir = _directorySet->find( addr );
      CacheState reqState = (type == Load) ? Shared : Modified;
      CacheState repState = dir.request( this, addr, reqState );

      assert( repState >= reqState );

      if( partialHit )
      {
         targetLine->state = repState;
         _updateLru( set, targetLine );
      }
      else
      {
         int lruWay = 0;
         int lruAge = 0;
         for( unsigned int w = 0; w < _assoc; ++w )
         {
            CacheLine& line = _lines[set][w];
            if( line.state == Invalid )
            {
               lruWay = w;
               break;
            }

            if( line.age > lruAge )
            {
               lruWay = w;
               lruAge = line.age;
            }
         }

         CacheLine& destLine = _lines[set][lruWay];

         // Tell directory about eviction
         if( destLine.state != Invalid )
         {
            uintptr_t evictAddr = destLine.tag << _tagShift;
            evictAddr |= set << _setShift;
            dir.request( this, evictAddr, Invalid );
         }

         destLine.tag = tag;
         destLine.state = repState;
         _updateLru( set, lruWay );
      }
   }

   return hit;
}

void Cache::setDirectories( DirectorySet* directorySet )
{
   _directorySet = directorySet;
}

void Cache::_updateLru( unsigned int set, CacheLine* usedLine )
{
   for( unsigned int w = 0; w < _assoc; ++w )
   {
      _lines[set][w].age += 1;
   }

   usedLine->age = 0;
}

void Cache::_updateLru( unsigned int set, unsigned int usedWay )
{
   for( unsigned int w = 0; w < _assoc; ++w )
   {
      _lines[set][w].age += 1;
   }

   _lines[set][usedWay].age = 0;
}

void Cache::downgrade( uintptr_t addr, CacheState newState )
{
   uintptr_t tag    = (addr & _tagMask) >> _tagShift;
   unsigned int set = (addr & _setMask) >> _setShift;

   CacheLine* targetLine = _find( set, tag );
   assert( targetLine != NULL );

   targetLine->state = newState;
}

CacheLine* Cache::_find( unsigned int set, uintptr_t tag ) const
{
   for( unsigned int w = 0; w < _assoc; ++w )
   {
      CacheLine& line = _lines[set][w];
      if( line.tag == tag && line.state != Invalid )
         return &line;
   }
   return NULL;
}
