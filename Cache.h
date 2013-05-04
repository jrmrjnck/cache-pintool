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
   int   accesses()          const { return _misses+_hits+_partialHits; }
   float hitRate()           const { return 100.0*_hits/accesses(); }
   float safeRate()          const { return 100.0*_safeAccesses/accesses(); }
   int   multilineAccesses() const { return _multilineAccesses; }
   int   downgrades()        const { return _downgrades; }
   int   rscFlushes()        const { return _rscFlush; }
   std::multimap<unsigned int,uintptr_t> downgradeMap( unsigned int count = 5 ) const;

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

   int _misses;
   int _hits;
   int _partialHits;

   int _safeAccesses;
   int _multilineAccesses;
   int _downgrades;
   int _rscFlush;

   std::map<uintptr_t,unsigned int> _downgradeCount;
};

#endif // !CACHE_H
