#include "Cache.h"
#include "Directory.h"

#include "pin.H"

#include <iostream>
#include <fstream>
#include <vector>

using namespace std;

const unsigned int CACHE_LINE_SIZE = 64;

typedef std::vector<Cache*> CacheList;
static CacheList caches;
static DirectorySet directorySet( 1, CACHE_LINE_SIZE );

int printUsage()
{
   std::cerr << "Usage: " << KNOB_BASE::StringKnobSummary() << std::endl;
   return -1;
}

void load( uintptr_t addr, unsigned int size, THREADID tid, void* v )
{
   caches[tid]->access( Cache::Load, addr, size );
}

void store( uintptr_t addr, unsigned int size, THREADID tid, void* v )
{
   caches[tid]->access( Cache::Store, addr, size );
}

void instrumentTrace( TRACE trace, void* v )
{
   for( BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl) )
   {
      for( INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins) )
      {
         if( INS_IsMemoryRead(ins) )
         {
            INS_InsertPredicatedCall( ins, 
                                      IPOINT_BEFORE, 
                                      reinterpret_cast<AFUNPTR>(load),
                                      IARG_MEMORYREAD_EA,
                                      IARG_MEMORYREAD_SIZE,
                                      IARG_THREAD_ID,
                                      IARG_PTR, v,
                                      IARG_END );
         }
         
         if( INS_IsMemoryWrite(ins) )
         {
            INS_InsertPredicatedCall( ins,
                                      IPOINT_BEFORE,
                                      reinterpret_cast<AFUNPTR>(store),
                                      IARG_MEMORYWRITE_EA,
                                      IARG_MEMORYWRITE_SIZE,
                                      IARG_THREAD_ID,
                                      IARG_PTR, v,
                                      IARG_END );
         }
      }
   }
}

void addCache( unsigned int tid, CONTEXT* ctxt, int flags, void* v )
{
   if( tid >= caches.size() )
   {
      caches.resize( tid + 1, NULL );
   }
   caches[tid] = new Cache( 32*KILO, CACHE_LINE_SIZE, 4 );
   caches[tid]->setDirectories( &directorySet );
}

void deleteCache( unsigned int tid, const CONTEXT* ctxt, int code, void* v )
{
   cout << "Cache " << tid << endl;
   caches[tid]->printStats();
}

void finish( int code, void* v )
{
}

int main(int argc, char *argv[])
{
   PIN_InitSymbols();

   if( PIN_Init(argc,argv) )
      return printUsage();

   TRACE_AddInstrumentFunction( instrumentTrace, &caches );
   PIN_AddThreadStartFunction( addCache, &caches );
   PIN_AddThreadFiniFunction( deleteCache, &caches );
   PIN_AddFiniFunction( finish, &caches );

   PIN_StartProgram();

   return 0;
}
