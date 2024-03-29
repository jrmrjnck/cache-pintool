#include "Cache.h"
#include "Util.h"
#include "Directory.h"

#include <cassert>
#include <iostream>

using namespace std;

Cache::Cache( size_t cacheSize, 
              size_t lineSize, 
              unsigned int assoc, 
              DirectorySet* directorySet )
 : _directorySet(directorySet)
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

   _misses      = 0;
   _hits        = 0;
   _partialHits = 0;

   _safeAccesses = 0;
   _multilineAccesses = 0;
   _downgrades = 0;
   _rscFlush = 0;
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
   if( targetLine != nullptr )
   {
      assert( targetLine->state != Invalid );
      if( type == Store && targetLine->state < Exclusive )
         partialHit = true;
      else
         hit = true;
   }

   if( hit )
   {
      ++_hits;

      if( type == Store )
         targetLine->state = Modified;

      if( targetLine->safe )
         ++_safeAccesses;
   }
   else
   {
      // Directory request needed for anything other than full hit
      bool safe;
      Directory& dir = _directorySet->find( addr );
      CacheState reqState = (type == Load) ? Shared : Modified;
      CacheState repState = dir.request( this, addr, reqState, &safe );

      assert( repState >= reqState );

      if( partialHit )
      {
         assert( repState == Modified );
         targetLine->state = repState;
         targetLine->safe  = safe;
         ++_partialHits;
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
         targetLine = &destLine;

         // Tell directory about eviction
         if( destLine.state != Invalid )
         {
            uintptr_t evictAddr = destLine.tag << _tagShift;
            evictAddr |= set << _setShift;
            _directorySet->find( evictAddr ).request( this, evictAddr, Invalid );
         }

         destLine.tag   = tag;
         destLine.state = repState;
         destLine.safe  = safe;

         ++_misses;
      }
   }

   _updateLru( set, targetLine );

   // Check if more lines need to be accessed
   uintptr_t endAddr = addr + length - 1;
   unsigned int endSet = (endAddr & _setMask) >> _setShift;
   if( endSet != set )
   {
      uintptr_t nextSetBase = (addr & ~_offsetMask) + (1 << _setShift);
      size_t curSetLen = _lineSize - (addr & _offsetMask);
      hit = hit && access( type, nextSetBase, length-curSetLen );
      ++_multilineAccesses;
   }

   return hit;
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

void Cache::downgrade( uintptr_t addr, CacheState newState, bool safe )
{
   uintptr_t tag    = (addr & _tagMask) >> _tagShift;
   unsigned int set = (addr & _setMask) >> _setShift;

   CacheLine* targetLine = _find( set, tag );

   assert( targetLine != nullptr );
   assert( newState == Invalid || newState == Shared );

   // Reactive SC flush condition
   if( targetLine->safe && !safe )
   {
      ++_rscFlush;
   }

   targetLine->state = newState;
   targetLine->safe  = safe;

   ++_downgrades;

   _downgradeCount[addr >> _setShift]++;
}

Cache::CacheLine* Cache::_find( unsigned int set, uintptr_t tag ) const
{
   for( unsigned int w = 0; w < _assoc; ++w )
   {
      CacheLine& line = _lines[set][w];
      if( line.tag == tag && line.state != Invalid )
         return &line;
   }
   return nullptr;
}

multimap<unsigned long int,uintptr_t> Cache::downgradeMap( unsigned int count ) const
{
   multimap<unsigned long int,uintptr_t> topAddrs;

   for( auto it = _downgradeCount.begin(); it != _downgradeCount.end(); ++it )
   {
      topAddrs.insert( make_pair(it->second,it->first) );

      if( topAddrs.size() > count )
         topAddrs.erase( topAddrs.begin() );
   }

   return topAddrs;
}
