#include "Cache.h"
#include "Directory.h"

#include "pin.H"

#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cassert>

using namespace std;

const unsigned int CACHE_LINE_SIZE = 64;

typedef std::vector<Cache*> CacheList;
static CacheList caches;
static DirectorySet directorySet( 1, CACHE_LINE_SIZE );

static PIN_MUTEX mutex;

int printUsage()
{
   std::cerr << "Usage: " << KNOB_BASE::StringKnobSummary() << std::endl;
   return -1;
}

void printIns( THREADID tid, void* v )
{
   char* s = static_cast<char*>(v);
   cout << tid << ": " << s << endl;
}

void load( uintptr_t addr, unsigned int size, THREADID tid, void* v )
{
   PIN_MutexLock( &mutex );
   //cout << tid << " L: " << size << " " << hex << addr << endl;
   caches[tid]->access( Cache::Load, addr, size );
   PIN_MutexUnlock( &mutex );
}

void store( uintptr_t addr, unsigned int size, THREADID tid, void* v )
{
   PIN_MutexLock( &mutex );
   //cout << tid << " S: " << size << " " << hex << addr << endl;
   caches[tid]->access( Cache::Store, addr, size );
   PIN_MutexUnlock( &mutex );
}

void instrumentTrace( TRACE trace, void* v )
{
   for( BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl) )
   {
      for( INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins) )
      {
         // Leak this memory
         /*
          *char* text = strdup( INS_Disassemble(ins).c_str() );
          *INS_InsertCall( ins, 
          *                IPOINT_BEFORE, 
          *                reinterpret_cast<AFUNPTR>(printIns),
          *                IARG_THREAD_ID,
          *                IARG_PTR, text,
          *                IARG_END );
          */

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
      caches.resize( tid + 1, nullptr );
   }
   caches[tid] = new Cache( 512*KILO, CACHE_LINE_SIZE, 4, &directorySet );
   cout << "Cache " << tid << " = " << hex << caches[tid] << endl;
}

void finish( int code, void* v )
{
   ofstream file( "safeaccess.log" );
   assert( file.good() );

   for( unsigned int i = 0; i < caches.size(); ++i )
   {
      file << "Cache " << i << endl;
      caches[i]->printStats( file );
      delete caches[i];
   }
   caches.clear();
}

int main(int argc, char *argv[])
{
   PIN_InitSymbols();

   if( PIN_Init(argc,argv) )
      return printUsage();

   PIN_MutexInit( &mutex );

   TRACE_AddInstrumentFunction( instrumentTrace, &caches );
   PIN_AddThreadStartFunction( addCache, &caches );
   PIN_AddFiniFunction( finish, &caches );

   PIN_StartProgram();

   return 0;
}
