#include "Cache.h"
#include "Directory.h"

#include "pin.H"

#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cassert>
#include <iomanip>

using namespace std;

const unsigned int CACHE_SIZE = 256*KILO;
const unsigned int CACHE_LINE_SIZE = 64;
const unsigned int CACHE_ASSOCIATIVITY = 8;

typedef std::vector<Cache*> CacheList;
static CacheList caches;
static DirectorySet directorySet( 2, CACHE_LINE_SIZE );

static PIN_MUTEX mutex;

static KNOB<string> outputFile(KNOB_MODE_WRITEONCE, "pintool",
                               "o", "safeaccess.log", "Specify output file name" );
static KNOB<bool> allowReverse(KNOB_MODE_WRITEONCE, "pintool",
                               "r", "false", "Allow reverse transitions (unsafe to safe)" );

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
   caches[tid] = new Cache( CACHE_SIZE, 
                            CACHE_LINE_SIZE, 
                            CACHE_ASSOCIATIVITY, 
                            &directorySet );
   //cout << "Cache " << tid << " = " << hex << caches[tid] << endl;
}

void finish( int code, void* v )
{
   ofstream file( outputFile.Value().c_str() );
   assert( file.good() );

   file.precision(3);
   file << fixed;
   file << endl;

   file << setw(8) << ""
        << setw(10) << "Total Accesses"
        << setw(11) << "Hit Rate" 
        << setw(12) << "Safe Rate" 
        //<< setw(15) << "Multiline" 
        << setw(13) << "Downgrades" 
        << setw(13) << "RSC Flushes" 
        << endl;

   unsigned long int totalAccesses = 0;
   unsigned long int totalHits = 0;
   unsigned long int totalSafe = 0;
   unsigned long int totalDowngrades = 0;
   unsigned long int totalRscFlushes = 0;

   map<uintptr_t, unsigned long int> totalDowngradeCount;
   uintptr_t totalTopAddr;
   unsigned long int totalTopCount = 0;

   for( unsigned int i = 0; i < caches.size(); ++i )
   {
      file << "Cache " << i;

      const Cache& c = *caches[i];

      totalAccesses += c.accesses();
      totalHits     += c.hitRate() * c.accesses();
      totalSafe     += c.safeRate() * c.accesses();
      totalDowngrades += c.downgrades();
      totalRscFlushes += c.rscFlushes();

      file << setw(15) << c.accesses()
           << setw(10) << 100.0*c.hitRate() << "%"
           << setw(11) << 100.0*c.safeRate() << "%"
           //<< setw(15) << c.multilineAccesses()
           << setw(13) << c.downgrades()
           << setw(13) << c.rscFlushes()
           /*<< endl*/;

      // Print the most common downgrades from this cache
      const auto& dm = c.downgradeMap( 3 );
      for( auto it = dm.rbegin(); it != dm.rend(); ++it )
      {
         file << " (" << hex << it->second << " : " 
              << fixed << (100.0*it->first/c.downgrades()) << "%)";
      }

      // Add all downgrades into total
      const auto& dc = c.downgradeCount();
      for( auto it = dc.begin(); it != dc.end(); ++it )
      {
         auto curCount = totalDowngradeCount[it->first];
         curCount += it->second;
         if( curCount > totalTopCount )
         {
            totalTopAddr = it->first;
            totalTopCount = curCount;
         }
         totalDowngradeCount[it->first] = curCount;
      }

      file << dec << endl;

      delete caches[i];
   }
   caches.clear();

   file << "Totals ";
   file << setw(15) << totalAccesses
        << setw(10) << 100.0*totalHits/totalAccesses << "%"
        << setw(11) << 100.0*totalSafe/totalAccesses << "%"
        << setw(13) << totalDowngrades
        << setw(13) << totalRscFlushes;

   file << " (" << hex << totalTopAddr << " : "
        << fixed << (100.0*totalTopCount/totalDowngrades) << "%)";

   file << dec << endl << endl;

   directorySet.printStats( file );

   file.close();
}

int main(int argc, char *argv[])
{
   PIN_InitSymbols();

   if( PIN_Init(argc,argv) )
      return printUsage();

   directorySet.setAllowReverseTransition( allowReverse.Value() );

   PIN_MutexInit( &mutex );

   TRACE_AddInstrumentFunction( instrumentTrace, &caches );
   PIN_AddThreadStartFunction( addCache, &caches );
   PIN_AddFiniFunction( finish, &caches );

   PIN_StartProgram();

   return 0;
}
