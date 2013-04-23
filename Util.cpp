#include "Util.h"

bool isPowerOf2( int n )
{
   return ((n & (n - 1)) == 0);
}

int floorLog2( int n )
{
   int p = 0;

   if( n == 0 )
      return -1;

   if( n & 0xFFFF0000 ) { p += 16; n >>= 16; }
   if( n & 0x0000FF00 )	{ p +=  8; n >>=  8; }
   if( n & 0x000000F0 ) { p +=  4; n >>=  4; }
   if( n & 0x0000000C ) { p +=  2; n >>=  2; }
   if( n & 0x00000002 ) { p +=  1; }

   return p;
}

int ceilLog2( int n )
{
   return floorLog2(n-1) + 1;
}
