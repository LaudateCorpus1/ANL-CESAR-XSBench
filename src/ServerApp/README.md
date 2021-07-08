This source was adapted from John's "mmapspeed" application backported on 
03-JUN-2020

     $T/src/zhpe-support/step2/lf_tests/mmapspeed.c

The original mmapspeed.c source was an all-in-one implementation. In this
reworking it was hacked so that the “do_client()” function effectively gets
broken up into “start_client() + do_client_work() + stop_client()” functions. 
These three new functions are successively called from the "main()" function 
so that, in aggregate, there behavior mimics that of the original "do_client()" 
function. The "do_server()" function was simply renamed.

Then reworking now comprises three source files: mmapUtils.c, mmapUtils.h, 
and mmapApp.c. The mmapUtils.c contains everything mmapspeed.c down as far as 
the “usage()” functio, while mmapApp.c contains the remainder, the "usage()"
and "main()" functions. This separation allows us to create an object file that 
can be linked with other applications (XSBench, LMBench adn STREAM).

Use "make" to build the "./mmapApp" binary. 

This "mmapApp" binary should behave identically to "mmapspeed". Indeed it
should be possible to have "mmapspeed" run as the server half and "mmapApp"
run as the client half, or vice-versa.

Then in XSBench, I created short subroutines called “zhpeHack_mmap_alloc()” that basically calls “start_client()” and “zhpeHack_munmap_free()” that calls “stop_client()”.

To test using Gen-Z memory on svlpc180, decide on a port to use (e.g. 7512) 
and start "mmapApp 7512" on svlpc180. 

On a node with Gen-Z connectivity to svlpc180 (could be svlpc180 itself) run
a test using memory region of 4MiB with:

     mmapApp 7512 svlpc180 4M

The usage output from mmapApp (mmapspeed) looks like:

Usage:mmapApp [-o] [-t threads] <port> [<node> <mmap_len>]
All sizes may be postfixed with [kmgtKMGT] to specify the base units.
Lower case is base 10; upper case is base 2.
Server requires just port; client requires all 3 arguments.
Client only options:
 -o : run once and then server will exit
 -t <threads> : number of simultaneous threads, <mmap_len> buffer for each

