#ifndef CACHE_H
#define CACHE_H

#include <cstdlib>
#include <stdint.h>
#include <iostream>
#include <map>

const int KILO = 1024;
const int MEGA = KILO*KILO;
const int GIGA = KILO*MEGA;

class DirectorySet;

enum CacheState
{
   Invalid,
   Shared,
   Exclusive,
   Modified
};

class Cache
{
public:
   enum AccessType
   {
      Load,
      Store
   };

private:
   struct CacheLine
   {
      CacheLine() : tag(0), state(Invalid), age(0) {}

      uintptr_t tag;
      CacheState state;
      int age;
      bool safe;
   };

public:
   Cache( size_t cacheSize, 
          size_t lineSize,
          unsigned int assoc,
          DirectorySet* directorySet );
   ~Cache();

   bool access( AccessType type, uintptr_t addr, size_t length );

   unsigned int lineSize() const { return _lineSize; }

   void downgrade( uintptr_t addr, CacheState newState, bool safe );

   // Statistics interface
   unsigned long int accesses()          const { return _misses+_hits+_partialHits; }
   float             hitRate()           const { return static_cast<float>(_hits)/accesses(); }
   float             safeRate()          const { return static_cast<float>(_safeAccesses)/accesses(); }
   unsigned long int multilineAccesses() const { return _multilineAccesses; }
   unsigned long int downgrades()        const { return _downgrades; }
   unsigned long int rscFlushes()        const { return _rscFlush; }

   const std::map<uintptr_t, unsigned long int>& downgradeCount() const { return _downgradeCount; }
   std::multimap<unsigned long int,uintptr_t> downgradeMap( unsigned int count = 5 ) const;

private:
   void _updateLru( unsigned int set, CacheLine* usedLine );
   void _updateLru( unsigned int set, unsigned int usedWay );
   
   CacheLine* _find( unsigned int set, uintptr_t tag ) const;

private:
   unsigned int _sets;
   unsigned int _lineSize;
   unsigned int _assoc;

   uintptr_t _offsetMask;
   uintptr_t _setMask;
   int       _setShift;
   uintptr_t _tagMask;
   int       _tagShift;

   CacheLine** _lines;

   DirectorySet* _directorySet;

   unsigned long int _misses;
   unsigned long int _hits;
   unsigned long int _partialHits;

   unsigned long int _safeAccesses;
   unsigned long int _multilineAccesses;
   unsigned long int _downgrades;
   unsigned long int _rscFlush;

   std::map<uintptr_t,unsigned long int> _downgradeCount;
};

#endif // !CACHE_H
