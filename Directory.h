#ifndef DIRECTORY_H
#define DIRECTORY_H

#include "Cache.h"

#include <vector>
#include <map>
#include <stdint.h>
#include <iostream>

class Directory
{
   friend class DirectorySet;
public:
   Directory( unsigned int lineSize );

   CacheState request( Cache* cache, 
                       uintptr_t addr, 
                       CacheState reqState, 
                       bool* safe = nullptr );

private:
   unsigned int _addrShift;

   struct DirectoryEntry
   {
      DirectoryEntry() 
       : modified(false), 
         owner(nullptr),
         readOnly(true),
         shared(false)
      {}

      bool modified;
      std::vector<Cache*> caches;

      Cache* owner;
      bool readOnly;
      bool shared;
   };
   std::map<uintptr_t,DirectoryEntry> _dir;

   bool _allowReverseTransition;
};

class DirectorySet
{
public:
   DirectorySet( unsigned int numSites, unsigned int lineSize );

   // Return the Directory that is the homesite for the given addr
   Directory& find( uintptr_t addr );

   void setAllowReverseTransition( bool allow );

   void printStats( std::ostream& stream = std::cout ) const;

private:
   std::vector<Directory*> _sites;

   std::map<uintptr_t,unsigned int> _pageMap;
};

#endif // !DIRECTORY_H
