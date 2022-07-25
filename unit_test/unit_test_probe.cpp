#include <iostream>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

// a sort of unit test for one part of run_z_probe
// g++ -Wall -g unit_test_probe.cpp -o unit_test_probe

using namespace std;

#define EXTRA_PROBING 1
#define PROBING_ADAPTIVE_OVER 0.03
#define MULTIPLE_PROBING 2
#define PROBING_ADAPTIVE_RETRY_LIMIT 4
#define Z_CLEARANCE_MULTI_PROBE 5
#define MEASURE_BACKLASH_WHEN_PROBING 0

#define TOTAL_PROBING (MULTIPLE_PROBING + EXTRA_PROBING)

#define Z_PROBE_FEEDRATE_FAST (7*60)
#define Z_PROBE_FEEDRATE_SLOW (Z_PROBE_FEEDRATE_FAST / 2)

#define PROBE_TARE 0

#define LOOP_S_LE_N(VAR, S, N) for (uint8_t VAR=(S); VAR<=(N); VAR++)
#define LOOP_LE_N(VAR, N) LOOP_S_LE_N(VAR, 0, N)

#define TERN0(O,A)          _TERN(_ENA_1(O),0,A)    // OPTION ? 'A' : '0'
#define _TERN(E,V...)       __TERN(_CAT(T_,E),V)    // Prepend 'T_' to get 'T_0' or 'T_1'
#define __TERN(T,V...)      ___TERN(_CAT(_NO,T),V)  // Prepend '_NO' to get '_NOT_0' or '_NOT_1'
#define ___TERN(P,V...)     THIRD(P,V)              // If first argument has a comma, A. Else B.
#define THIRD(a,b,c,...) c
#define TERN_(O,A)          _TERN(_ENA_1(O),,A)     // OPTION ? 'A' : '<nul>'
// Macros to support option testing
#define _CAT(a,V...) a##V
#define CAT(a,V...) _CAT(a,V)

#define SERIAL_ECHOLNPGM(s) printf("%s\n", s);
void SERIAL_ECHOPGM(const char *s)
{
  printf(s);
}
void SERIAL_ECHOPGM(const char *a, float x, const char *b, float y,
  const char *c)
{
  cout << a << x << b << y << c;
}
void SERIAL_ECHO_F(float f, int p = 2)
{
  printf("%.*f", p, f);
}
void do_blocking_move_to_z(float z, float mm_s)
{
}

#define PGM_P const char*
#define PSTR(s) s
#define MMM_TO_MMS(rate) rate
#define RECIPROCAL(n) (1.0/(n))
//#define NAN (1.0/0.0) // math.h
const float z_probe_fast_mm_s = 0.0;

typedef const float const_float_t;
typedef const_float_t feedRate_t;

static bool tare()
{
  return false;
}

struct
{
  float measure_with_probe() { return 0; }
}
backlash;

struct
{
  float x, y, z;
} current_position;

static int s_argc, s_i = 1;
static char **s_argv;

static 
// returns true on failure
bool try_to_probe(PGM_P const plbl, const_float_t z_probe_low_point, const feedRate_t fr_mm_s, const bool scheck, const float clearance)
{
  if(s_i < s_argc)
  {
    current_position.z = atof(s_argv[s_i++]);
    return false;
  }
  std::cout << "no more arguments to return\n";
  return true;
}

float run_z_probe(const bool sanity_check = true)
{
  const float z_probe_low_point = 0.01;

  if (try_to_probe(PSTR("SLOW"), z_probe_low_point, MMM_TO_MMS(Z_PROBE_FEEDRATE_SLOW),
     sanity_check, Z_CLEARANCE_MULTI_PROBE) ) return NAN;
  float first_probe_z = current_position.z;

  #if EXTRA_PROBING > 0
    float probes[MULTIPLE_PROBING + EXTRA_PROBING + PROBING_ADAPTIVE_RETRY_LIMIT];
  #endif

  // debugging
  for(size_t i = 0; i < sizeof(probes)/sizeof(*probes); ++i)
    probes[i] = -1;

  #ifdef PROBING_ADAPTIVE_OVER
  uint8_t bad = 0;
  float average = NAN;
  probes[0] = first_probe_z;
  uint8_t probes_next = 1;
  bool again = true;
  // Keep retrying probing until the retry limit is reached or there are two
  // more good values than bad values.  If it passes all good values are
  // averaged.  This allows stopping at two good values which matching the
  // historic two probe values then average, while both detecting and retrying
  // inconsistent results.  If there is one bad value it will probe four times,
  // for two, six.  Good and bad values are determined by the configuration
  // difference PROBING_ADAPTIVE_OVER.  This is written assuming bad values
  // will be inconsistent with the only matches being among good values, or
  // close enough value to a good value as to not matter.  The more bad values
  // seen, the more the number of good values requied to verify that the
  // correct values are identified as good or bad.
  //
  // Loop retries left:
  //  probe
  //   for each probe point
  //    compare to each other probe
  //    count the number within the limit
  //    keep sum of good values
  //    if good - bad > 2
  //      average good; done

  // the first probe, p == 0, has already happened
  while(probes_next < MULTIPLE_PROBING + PROBING_ADAPTIVE_RETRY_LIMIT)
  {
    // If the probe won't tare, return
    if (TERN0(PROBE_TARE, tare())) return true;

    // Probe downward slowly to find the bed
    if (try_to_probe(PSTR("SLOW"), z_probe_low_point, MMM_TO_MMS(Z_PROBE_FEEDRATE_SLOW),
                     sanity_check, Z_CLEARANCE_MULTI_PROBE) ) return NAN;

    TERN_(MEASURE_BACKLASH_WHEN_PROBING, backlash.measure_with_probe());

    const float z = current_position.z;
    probes[probes_next++] = z;

    // For each entry compared to every other entry (and itself, which always
    // passes, to keep it simple).  Keeping a running of the sum for that set
    // of good values.
    for(uint8_t start = 0; start < probes_next; ++start)
    {
      float sum = 0;
      uint8_t good = 0;
      for(uint8_t i = 0; i < probes_next; ++i)
      {
        if(probes[start] == probes[i] ||
          fabsf(probes[start] - probes[i]) < PROBING_ADAPTIVE_OVER)
        {
          sum += probes[i];
          ++good;
        }
      }
      bad = probes_next - good;
      printf("start %u good %u bad %u avg %10f\n",
        start, good, bad, sum / good);
      if(good - bad >= 2)
      {
        again = false;
        average = sum * RECIPROCAL(good);
        break;
      }
    }

    // Small Z raise when probing again
    if(again)
      do_blocking_move_to_z(z + Z_CLEARANCE_MULTI_PROBE, z_probe_fast_mm_s);
    else
      break;
  }

  float minimum, maximum;
  minimum = maximum = probes[0];
  for(uint8_t i = 0; i < probes_next; ++i)
  {
    float v = probes[i];
    if(minimum > v)
      minimum = v;
    else if(maximum < v)
      maximum = v;
  }
  float worst = maximum - minimum;;

  if(bad)
    SERIAL_ECHOPGM("Warning likely bad probe, difference: ");
  else
    SERIAL_ECHOPGM("Debug probe, difference: ");
  SERIAL_ECHO_F(worst, 6);
  SERIAL_ECHOPGM("  X:", current_position.x,
    " Y:", current_position.y, " Z: ");
  for(uint8_t p = 0; p < probes_next; ++p)
  {
      if(p)
          SERIAL_ECHOPGM(" ");
      SERIAL_ECHO_F(probes[p], 6);
  }
  SERIAL_ECHOLNPGM("");

  if(again)
  {
    SERIAL_ECHOLNPGM("Error probe results unreliable aborting.");
    return NAN;
  }

  // average the two best probe values
  return average;
  #endif
}

int main(int argc, char **argv)
{
  s_argc = argc;
  s_argv = argv;
  float z = run_z_probe();
  cout << "run_z_probe result " << z << endl;
  return 0;
}
