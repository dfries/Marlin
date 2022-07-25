#include <iostream>
#include <stdint.h>
#include <stdlib.h>

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
#define NAN (1.0/0.0)
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
    float probes[3];
    // debugging eliminate
    float in_order[MULTIPLE_PROBING + EXTRA_PROBING + PROBING_ADAPTIVE_RETRY_LIMIT];
  #endif

  // debugging
  for(size_t i = 0; i < sizeof(probes)/sizeof(*probes); ++i)
    probes[i] = -1;

  #ifdef PROBING_ADAPTIVE_OVER
  uint8_t retry = 0;
  float worst = 0;
  probes[0] = first_probe_z;
  in_order[0] = first_probe_z;
  uint8_t probes_next = 1;
  uint8_t in_order_next = 1;
  // the first probe, p == 0, has already happened
  for(uint8_t p = 1; p < MULTIPLE_PROBING + PROBING_ADAPTIVE_RETRY_LIMIT; ++p)
  {
    // If the probe won't tare, return
    if (TERN0(PROBE_TARE, tare())) return true;

    // Probe downward slowly to find the bed
    if (try_to_probe(PSTR("SLOW"), z_probe_low_point, MMM_TO_MMS(Z_PROBE_FEEDRATE_SLOW),
                     sanity_check, Z_CLEARANCE_MULTI_PROBE) ) return NAN;

    TERN_(MEASURE_BACKLASH_WHEN_PROBING, backlash.measure_with_probe());

    const float z = current_position.z;
    in_order[in_order_next++] = z;
    // Insert Z measurement into probes[]. Keep it sorted ascending.
    LOOP_LE_N(i, probes_next) {                            // Iterate the saved Zs to insert the new Z
      if (i == probes_next || probes[i] > z) {                              // Last index or new Z is smaller than this Z
        for (int8_t m = probes_next; --m >= i;) probes[m + 1] = probes[m];  // Shift items down after the insertion point
        probes[i] = z;                                            // Insert the new Z measurement
        break;                                                    // Only one to insert. Done!
      }
    }
    ++probes_next;

    SERIAL_ECHOPGM("probes ");
    SERIAL_ECHO_F(probes[0], 6);
    SERIAL_ECHOPGM(" ");
    SERIAL_ECHO_F(probes[1], 6);
    SERIAL_ECHOPGM(" ");
    SERIAL_ECHO_F(probes[2], 6);
    SERIAL_ECHOLNPGM("");

    // once there are three eliminate the worst
    if(probes_next == 3)
    {
      const float median = probes[1];
      const float max_diff = probes[2] - median;
      const float min_diff = median - probes[0];
      float toss;
      if (max_diff > min_diff)
      {
        toss = max_diff;
        // probe_next goes down drops the last entry
      }
      else
      {
        toss = min_diff;
        // shift to eliminate the first one
        probes[0] = probes[1];
        probes[1] = probes[2];
      }
      probes_next = 2;
      if(worst < toss)
        worst = toss;
    }

    // compare the two left
    const float difference = probes[1] - probes[0];
    if(worst < difference)
      worst = difference;

    if(difference < PROBING_ADAPTIVE_OVER)
      break;

    if(++retry > PROBING_ADAPTIVE_RETRY_LIMIT)
      break;

    // Small Z raise when probing again
    do_blocking_move_to_z(z + Z_CLEARANCE_MULTI_PROBE, z_probe_fast_mm_s);
  }

  if(retry)
    SERIAL_ECHOPGM("Warning likely bad probe, difference: ");
  else
    SERIAL_ECHOPGM("Debug probe, difference: ");
  SERIAL_ECHO_F(worst, 6);
  SERIAL_ECHOPGM("  X:", current_position.x,
    " Y:", current_position.y, " Z: ");
  for(uint8_t p = 0; p < in_order_next; ++p)
  {
      if(p)
          SERIAL_ECHOPGM(" ");
      SERIAL_ECHO_F(in_order[p], 6);
  }
  SERIAL_ECHOLNPGM("");

  // Checking the elimiate the worst
  SERIAL_ECHOPGM("Samples averaged: ");
  SERIAL_ECHO_F(probes[0], 6);
  SERIAL_ECHOPGM(" ");
  SERIAL_ECHO_F(probes[1], 6);
  // don't print probes[2] which was discarded and isn't averaged
  SERIAL_ECHOLNPGM("");

  if(retry > PROBING_ADAPTIVE_RETRY_LIMIT)
  {
    SERIAL_ECHOLNPGM("Error probe results unreliable aborting.");
    return NAN;
  }

  // average the two best probe values
  return (probes[0] + probes[1]) * RECIPROCAL(2);
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
