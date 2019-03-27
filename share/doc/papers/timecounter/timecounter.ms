.EQ
delim øø
.EN
.\"
.\" ----------------------------------------------------------------------------
.\" "THE BEER-WARE LICENSE" (Revision 42):
.\" <phk@login.dknet.dk> wrote this file.  As long as you retain this notice you
.\" can do whatever you want with this stuff. If we meet some day, and you think
.\" this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
.\" ----------------------------------------------------------------------------
.\"
.\" $FreeBSD$
.\"
.if n .ND
.TI
Timecounters: Efficient and precise timekeeping in SMP kernels.
.AA
.A "Poul-Henning Kamp" "The FreeBSD Project"
.AB
The FreeBSD timecounters are an architecture-independent implementation
of a binary timescale using whatever hardware support is at hand  
for tracking time.  The binary timescale converts using simple
multiplication to canonical timescales based on micro- or nano-seconds
and can interface seamlessly to the NTP PLL/FLL facilities for clock
synchronisation.  Timecounters are implemented using lock-less
stable-storage based primitives which scale efficiently in SMP   
systems.  The math and implementation behind timecounters will 
be detailed as well as the mechanisms used for synchronisation. \**
.AE
.FS
This paper was presented at the EuroBSDcon 2002 conference in Amsterdam.
.FE
.1C
.SH
Introduction
.PP
Despite digging around for it, I have not been able to positively
identify the first computer which knew the time of day.
The feature probably arrived either from the commercial side
so service centres could bill computer cycles to customers or from
the technical side so computers could timestamp external events,
but I have not been able to conclusively nail the first implementation down.
.LP
But there is no doubt that it happened very early in the development
of computers
and if systems like the ``SAGE'' [SAGE] did not know what time
it was I would be amazed.
.LP
On the other hand, it took a long time for a real time clock to
become a standard feature:
.LP
The ``Apple ]['' computer
had neither in hardware or software any notion what time it was.
.LP
The original ``IBM PC'' did know what time it was, provided you typed
it in when you booted it, but it forgot when you turned it off.
.LP
One of the ``advanced technologies'' in the ``IBM PC/AT'' was a battery
backed CMOS chip which kept track of time even when the computer
was powered off.
.LP
Today we expect our computers to know the time, and with network
protocols like NTP we will usually find that they do, give and
take some milliseconds.
.LP
This article is about the code in the FreeBSD kernel which keeps
track of time.
.SH
Time and timescale basics
.PP
Despite the fact that time is the physical quantity (or maybe entity
?) about which we know the least, it is at the same time [sic!] what we
can measure with the highest precision of all physical quantities.
.LP
The current crop of atomic clocks will neither gain nor lose a
second in the next couple hundred million years, provided we
stick to the preventative maintenance schedules.  This is a feat
roughly in line with to knowing the circumference of the Earth
with one micrometer precision, in real time.
.LP
While it is possible to measure time by means other than oscillations,
for instance transport or consumption of a substance at a well-known
rate, such designs play no practical role in time measurement because
their performance is significantly inferior to oscillation based
designs.
.LP
In other words, it is pretty fair to say that all relevant
timekeeping is based on oscillating phenomena:
.TS
center;
l l.
sun-dial	Earths rotation about its axis.
calendar	Ditto + Earths orbit around the sun.
clockwork	Mechanical oscillation of pendulum.
crystals	Mechanical resonance in quartz.
atomic	Quantum-state transitions in atoms.
.TE
.LP
We can therefore with good fidelity define ``a clock'' to be the
combination of an oscillator and a counting mechanism:
.LP
.if t .PSPIC fig3.eps
.LP
The standard second is currently defined as
.QP
The duration of  9,192,631,770 periods of the radiation corresponding to the transition between the two hyperfine levels of the ground state of the caesium 133 atom.
.LP
and we have frequency standards which are able to mark a sequence of 
such seconds
with an error less than ø2 cdot 10 sup{-15}ø [DMK2001] with commercially 
available products doing better than ø1 cdot 10 sup{-14}ø [AG2002].
.LP
Unlike other physical units with a conventionally defined origin,
longitude for instance, the ephemeral nature of time prevents us
from putting a stake in the ground, so to speak, and measure from
there.  For measuring time we have to rely on ``dead reckoning'',
just like the navigators before Harrison built his clocks [RGO2002]:
We have to tally how far we went from our reference point, keeping a
running total at all times, and use that as our estimated position.
.LP
The upshot of this is, that we cannot define a timescale by any
other means than some other timescale(s).
.LP
``Relative time'' is a time interval between two events, and for this
we only need to agree on the rate of the oscillator.
.LP
``Absolute time'' consists of a well defined point in time and the
time interval since then, this is a bit more tricky.
.LP
The Internationally agreed upon TAI and the UTC timescales 
starts at (from a physics point of view) arbitrary points in time
and progresses in integral intervals of the standard second, with the
difference being that UTC does tricks to the counting to stay roughly
in sync with Earths rotation \**.
.FS
The first atomic based definition actually operated in a different way:
each year would have its own value determined for the frequency of the
caesium resonance, selected so as to match the revolution rate of the
Earth.  This resulted in time-intervals being very unwieldy business,
and more and more scientists realized that that the caesium resonance
was many times more stable than the angular momentum of the Earth.
Eventually the new leap-second method were introduced in 1972.
It is interesting to note that the autumn leaves falling on the
northern hemisphere affects the angular momentum enough to change 
the Earths rotational rate measurably.
.FE
.LP
TAI is defined as a sequence of standard seconds (the first timescale),
counted from January 1st 1958 (the second timescale).
.LP
UTC is defined basically the same way, but every so often a leap-second
is inserted (or theoretically deleted) to keep UTC synchronised
with Earths rotation.
.LP
Both the implementation of these two, and a few others speciality 
timescales are the result of the
combined efforts of several hundred atomic frequency standards in
various laboratories and institutions throughout the world, all
reporting to the BIPM in Paris who calculate the ``paper clock'' which
TAI and UTC really are using a carefully designed weighting algorithm \**. 
.FS
The majority of these clocks are model 5071A from Agilent (the test
and measurement company formerly known as ``Hewlett-Packard'') which
count for as much as 85% of the combined weight.
A fact the company deservedly is proud of.
The majority of the remaining weight is assigned to a handful of big
custom-design units like the PTB2 and NIST7.
.FE
.LP
Leap seconds are typically announced six to nine months in advance,
based on precise observations of median transit times of stars and VLBI
radio astronomy of very distant quasars.
.LP
The perceived wisdom of leap-seconds have been gradually decreasing
in recent years, as devices and products with built-in calendar
functionality becomes more and more common and people realize that
user input or software upgrades are necessary to instruct the
calendar functionality about upcoming leap seconds.
.SH
UNIX timescales
.PP
UNIX systems use a timescale which pretends to be UTC, but defined
as the count of standard seconds since 00:00:00 01-01-1970 UTC,
ignoring the leap-seconds.  This definition has never been perceived
as wise.
.LP
Ignoring leap seconds means that unless some trickery is performed
when a leap second happens on the UTC scale, UNIX clocks would be
one second off.  Another implication is that the length of a
time interval calculated on UNIX time_t variables, can be up to 22
(and counting) seconds wrong relative to the same time interval
measured on the UTC timescale.
.LP
Recent efforts have tried to make the NTP protocol make up for this
deficiency by transmitting the UTC-TAI offset as part of the protocol.
[MILLS2000A]
.LP
Fractional seconds are represented two ways in UNIX, ``timeval'' and
``timespec''.  Both of these formats are two-component structures
which record the number of seconds, and the number of microseconds
or nanoseconds respectively.
.LP
This unfortunate definition makes arithmetic on these two formats
quite expensive to perform in terms of computer instructions:
.DS
.ps -1
/* Subtract timeval from timespec */
t3.tv_sec = t1.tv_sec - t2.tv_sec;
t3.tv_nsec = t1.tv_nsec -
             t2.tv_usec * 1000;
if (t3.tv_nsec >= 1000000000) {
    t3.tv_sec++;
    t3.tv_nsec -= 1000000000;
} else if (t3.tv_nsec < 0) {
    t3.tv_sec--;
    t3.tv_nsec += 1000000000;
}
.ps +1
.DE
.LP
While nanoseconds will probably be enough for most timestamping
tasks faced by UNIX computers for a number of years, it is an
increasingly uncomfortable situation that CPU clock periods and
instruction timings are already not representable in the standard
time formats available on UNIX for consumer grade hardware,
and the first POSIX mandated API, \fCclock_getres(3)\fP has 
already effectively reached end of life as a result of this.
.LP
Hopefully the various standards bodies will address this issue
better in the future.
.SH
Precision, Stability and Resolution
.PP
Three very important terms in timekeeping are ``precision'', 
``stability'' and ``resolution''.
While the three words may seem to describe somewhat the
same property in most uses, their use in timekeeping covers three
very distinct and well defined properties of a clock.
.LP
Resolution in clocks is simply a matter of the step-size of the
counter or in other words: the rate at which it steps.
A counter running on a 1 MHz frequency will have a resolution
of 1 microsecond.
.LP
Precision talks about how close to the intended rate the clock runs,
stability about how much the rate varies and resolution about the
size of the smallest timeinterval we can measure.
.LP
From a quality point of view, Stability is a much more
valuable property than precision, this is probably best explained
using a graphic illustration of the difference between the two
concepts:
.LP
.if t .PSPIC fig1.eps
.LP
In the top row we have instability, the bullet holes are spread over
a large fraction of the target area.
In the bottom row, the bullets all hit in a very small area.
.LP
On the left side, we have lack of precision, the holes obviously are
not centred on the target, a systematic offset exists.
In the right side we have precision, the bullets are centred on
the target \**.
.FS
We cannot easily get resolution into this analogy, the obvious
representation as the diameter of the bullet-hole is not correct,
it would have to be the grid or other pattern of locations where
the bullet could possibly penetrate the target material, but this
gets too quantum-mechanical-oid to serve the instructional purpose.
.FE
.LP
Transposing these four targets to actual clocks, the situation
could look like the following plots:
.LP
.if t .PSPIC fig2.eps
.LP
On the x-axis we have time and on the y-axis how wrong the clock
was at a given point in time.
.LP
The reason atomic standards are such a big deal in timekeeping is
that they are incredibly stable: they are able to generate an oscillation
where the period varies by roughly a millionth of a billonth of a
second in long term measurements.
.LP
They are in fact not nearly as precise as they are stable, but as
one can see from the graphic above, a stable clock which is not
precise can be easily corrected for the offset and thus calibrated
is as good as any clock.
.LP
This lack of precision is not necessarily a flaw in these kinds of
devices, once you get into the ø10 cdot 10 sup{-15}ø territory
things like the blackbody spectrum at the particular absolute 
temperature of the clocks hardware and general relativistic
effects mostly dependent on the altitude above earths center
has to be corrected for \**. 
.FS
This particularly becomes an issue with space-based atomic standards
as those found on the ``Navstar'' GPS satellites.
.FE
.SH
Design goals of timecounters
.PP
After this brief description of the major features of the local
landscape, we can look at the design goals of timecounters in detail:
.LP
.I "Provide timestamps in timeval and timespec formats,"
.IP
This is obviously the basic task we have to solve, but as was noted
earlier, this is in no way the performance requirement.
.LP
.I "on both the ``uptime'' and the POSIX timescales,"
.IP
The ``uptime'' timescale is convenient for time intervals which are
not anchored in UTC time: the run time of processes, the access
time of disks and similar.
.IP
The uptime timescale counts seconds starting from when the system
is booted.  The POSIX/UTC timescale is implemented by adding an
estimate of the POSIX time when the system booted to the uptime
timescale.
.LP
.I "using whatever hardware we have available at the time,"
.IP
Which in a subtle way also implies ``be able to switch from one
piece of hardware to another on the fly'' since we may not know
right up front what hardware we have access to and which is
preferable to use.
.LP
.I "while supporting time the NTP PLL/FLL discipline code,"
.IP
The NTP kernel PLL/FLL code allows the local clock and timescale
to be synchronised or syntonised to an external timescale either
via network packets or hardware connection.  This also implies
that the rate and phase of the timescale must be manoeuvrable
with sufficient resolution.
.LP
.I "and providing support for the RFC 2783 PPS API,"
.IP
This is mainly for the benefit of the NTPD daemons communication
with external clock or frequency hardware, but it has many other
interesting uses as well [PHK2001].
.LP
.I "in a SMP efficient way."
.IP
Timestamps are used many places in the kernel and often at pretty
high rate so it is important that the timekeeping facility
does not become a point of CPU or lock contention.
.SH
Timecounter timestamp format.
.PP
Choosing the fundamental timestamp format for the timecounters is
mostly a question of the resolution and steer-ability requirements.
.LP
There are two basic options on contemporary hardware: use a 32 bit
integer for the fractional part of seconds, or use a 64 bit which
is computationally more expensive.
.LP
The question therefore reduced to the somewhat simpler: can we get
away with using only 32 bit ?
.LP
Since 32 bits fractional seconds have a resolution of slightly
better than quarter of a nanosecond (.2328 nsec) it can obviously
be converted to nanosecond resolution struct timespec timestamps
with no loss of precision, but unfortunately not with pure 32 bit
arithmetic as that would result in unacceptable rounding errors.
.LP
But timecounters also need to represent the clock period of the
chosen hardware and this hardware might be the GHz range CPU-clock.
The list of clock frequencies we could support with 32 bits are:
.TS
center;
l l n l.
ø2 sup{32} / 1ø	ø=ø	4.294	GHz
ø2 sup{32} / 2ø	ø=ø	2.147	GHz
ø2 sup{32} / 3ø	ø=ø	1.432	GHz
\&...
ø2 sup{32} / (2 sup{32}-1)ø	ø=ø	1.000	Hz
.TE
We can immediately see that 32 bit is insufficient to faithfully
represent clock frequencies even in the low GHz area, much less in
the range of frequencies which have already been vapourwared by
both IBM, Intel and AMD.
QED: 32 bit fractions are not enough.
.LP
With 64 bit fractions the same table looks like:
.TS
center;
l l r l.
ø2 sup{64} / 1ø	ø=ø	ø 18.45 cdot 10 sup{9}ø	GHz
ø2 sup{64} / 2ø	ø=ø	ø 9.223 cdot 10 sup{9}ø	GHz
\&...
ø2 sup{64} / 2 sup{32}ø	ø=ø	4.294	GHz
\&...
ø2 sup{64} / (2 sup{64}-1)ø	ø=ø	1.000	Hz
.TE
And the resolution in the 4 GHz frequency range is approximately one Hz.
.LP
The following format have therefore been chosen as the basic format
for timecounters operations:
.DS
.ps -1
struct bintime {
    time_t  sec;
    uint64_t frac;
};
.ps +1
.DE
Notice that the format will adapt to any size of time_t variable,
keeping timecounters safely out of the ``We SHALL prepare for the
Y2.038K problem'' war zone.
.LP
One beauty of the bintime format, compared to the timeval and
timespec formats is that it is a binary number, not a pseudo-decimal
number.  If compilers and standards allowed, the representation
would have been ``int128_t'' or at least ``int96_t'', but since this
is currently not possible, we have to express the simple concept
of multiword addition in the C language which has no concept of a
``carry bit''.
.LP
To add two bintime values, the code therefore looks like this \**:
.FS
If the reader suspects the '>' is a typo, further study is suggested.
.FE
.LP
.DS
.ps -1
uint64_t u;

u = bt1->frac;
bt3->frac = bt1->frac + bt2->frac;
bt3->sec  = bt1->sec  + bt2->sec;
if (u > bt3->frac)
    bt3->sec += 1;
.ps +1
.DE
.LP
An important property of the bintime format is that it can be
converted to and from timeval and timespec formats with simple
multiplication and shift operations as shown in these two
actual code fragments:
.DS
.ps -1
void
bintime2timespec(struct bintime *bt,
                 struct timespec *ts)
{

    ts->tv_sec = bt->sec;
    ts->tv_nsec = 
      ((uint64_t)1000000000 * 
      (uint32_t)(bt->frac >> 32)) >> 32;
}
.ps +1
.DE
.DS
.ps -1
void
timespec2bintime(struct timespec *ts,
                 struct bintime *bt)
{

    bt->sec = ts->tv_sec;
    /* 18446744073 = 
      int(2^64 / 1000000000) */
    bt->frac = ts->tv_nsec * 
      (uint64_t)18446744073LL; 
}
.ps +1
.DE
.LP
.SH
How timecounters work
.PP
To produce a current timestamp the timecounter code
reads the hardware counter, subtracts a reference
count to find the number of steps the counter has
progressed since the reference timestamp.
This number of steps is multiplied with a factor
derived from the counters frequency, taking into account
any corrections from the NTP PLL/FLL and this product
is added to the reference timestamp to get a timestamp.
.LP
This timestamp is on the ``uptime'' time scale, so if
UNIX/UTC time is requested, the estimated time of boot is
added to the timestamp and finally it is scaled to the
timeval or timespec if that is the desired format.
.LP
A fairly large number of functions are provided to produce
timestamps, depending on the desired timescale and output
format:
.TS
center;
l r r.
Desired	uptime	UTC/POSIX
Format	timescale	timescale
_
bintime	binuptime()	bintime()
timespec	nanouptime()	nanotime()
timeval	microuptime()	microtime()
.TE
.LP
Some applications need to timestamp events, but are not
particular picky about the precision.
In many cases a precision of tenths or hundreds of 
seconds is sufficient.
.LP
A very typical case is UNIX file timestamps:
There is little point in spending computational resources getting an
exact nanosecond timestamp, when the data is written to
a mechanical device which has several milliseconds of unpredictable
delay before the operation is completed.
.LP
Therefore a complementary shadow family of timestamping functions 
with the prefix ``get'' have been added.
.LP
These functions return the reference
timestamp from the current timehands structure without going to the
hardware to determine how much time has elapsed since then.
These timestamps are known to be correct to within rate at which
the periodic update runs, which in practice means 1 to 10 milliseconds.
.SH
Timecounter math
.LP
The delta-count operation is straightforward subtraction, but we
need to logically AND the result with a bit-mask with the same number
(or less) bits as the counter implements,
to prevent higher order bits from getting set when the counter rolls over:
.DS 
.ce 
.EQ
Delta Count = (Count sub{now} - Count sub{ref}) ~ BITAND ~ mask
.EN
.DE
The scaling step is straightforward.
.DS
.ce 
.EQ
T sub{now} = Delta Count cdot R sub{counter} + T sub{ref}
.EN
.DE
The scaling factor øR sub{counter}ø will be described below.
.LP
At regular intervals, scheduled by \fChardclock()\fP, a housekeeping
routine is run which does the following:
.LP
A timestamp with associated hardware counter reading is elevated
to be the new reference timecount:
.DS

.ce
.EQ
Delta Count = (Count sub{now} - Count sub{ref}) ~ BITAND ~ mask
.EN

.ce
.EQ
T sub{now} = Delta Count cdot R sub{counter}
.EN

.ce
.EQ
Count sub{ref} = Count sub{now}
.EN

.ce
.EQ
T sub{ref} = T sub{now}
.EN
.DE
.LP
If a new second has started, the NTP processing routines are called
and the correction they return and the counters frequency is used
to calculate the new scaling factor øR sub{counter}ø:
.DS
.ce
.EQ
R sub{counter} = {2 sup{64} over Freq sub{counter}} cdot ( 1 + R sub{NTP} )
.EN
.DE
Since we only have access to 64 bit arithmetic, dividing something
into ø2 sup{64}ø is a problem, so in the name of code clarity
and efficiency, we sacrifice the low order bit and instead calculate:
.DS
.ce
.EQ
R sub{counter} = 2 cdot {2 sup{63} over Freq sub{counter}} cdot ( 1 + R sub{NTP} )
.EN
.DE
The øR sub{NTP}ø correct factor arrives as the signed number of
nanoseconds (with 32 bit binary fractions) to adjust per second.
This quasi-decimal number is a bit of a square peg in our round binary
hole, and a conversion factor is needed.
Ideally we want to multiply this factor by:
.DS
.ce
.EQ
2 sup {64} over {10 sup{9} cdot 2 sup{32}} = 4.294967296
.EN
.DE
This is not a nice number to work with.
Fortunately, the precision of this correction is not critical, we are
within an factor of a million of the ø10 sup{-15}ø performance level
of state of the art atomic clocks, so we can use an approximation
on this term without anybody noticing.
.LP
Deciding which fraction to use as approximation needs to carefully
consider any possible overflows that could happen.
In this case the correction may be as large as \(+- 5000 PPM which
leaves us room to multiply with about 850 in a multiply-before-divide
setting.
Unfortunately, there are no good fractions which multiply with less
than 850 and at the same time divide by a power of two, which is
desirable since it can be implemented as a binary shift instead of
an expensive full division.
.LP
A divide-before-multiply approximation necessarily results in a loss
of lower order bits, but in this case dividing by 512 and multiplying
by 2199 gives a good approximation where the lower order bit loss is
not a concern:
.DE
.EQ
2199 over 512 = 4.294921875
.EN
.DE
The resulting error is an systematic under compensation of 10.6PPM 
of the requested change, or ø1.06 cdot 10 sup -14ø per nanosecond
of correction.
This is perfectly acceptable.
.LP
Putting it all together, including the one bit we put on the alter for the
Goddess of code clarity, the formula looks like this:
.DS
.ce
.EQ
R sub{counter} = 2 cdot {{2 sup{63} + 2199 cdot {R sub{NTP}} over 1024} over Freq sub{counter}}
.EN
.DE
Presented here in slightly unorthodox format to show the component arithmetic
operations as they are carried out in the code.
.SH
Frequency of the periodic update
.PP
The hardware counter should have a long enough
period, ie, number of distinct counter values divided by
frequency, to not roll over before our periodic update function
has had a chance to update the reference timestamp data.
.LP
The periodic update function is called from \fChardclock()\fP which
runs at a rate which is controlled by the kernel parameter
.I HZ .
.LP
By default HZ is 100 which means that only hardware with a period
longer than 10 msec is usable.
If HZ is configured higher than 1000, an internal divider is
activated to keep the timecounter periodic update running 
no more often than 2000 times per second.
.LP
Let us take an example:
At HZ=100 a 16 bit counter can run no faster than:
.DS
.ce
.EQ
2 sup{16} cdot {100 Hz} = 6.5536 MHz
.EN
.DE
Similarly, if the counter runs at 10MHz, the minimum HZ is
.DS
.ce
.EQ
{10 MHz} over {2 sup{16}} = 152.6 Hz
.EN
.DE
.LP
Some amount of margin is of course always advisable,
and a factor two is considered prudent.
.LP
.SH
Locking, lack of ...
.PP
Provided our hardware can be read atomically, that our arithmetic
has enough bits to not roll over and that our clock frequency is
perfectly, or at least sufficiently, stable, we could avoid the
periodic update function, and consequently disregard the entire
issue of locking.
We are seldom that lucky in practice.
.LP
The straightforward way of dealing with meta data updates is to
put a lock of some kind on the data and grab hold of that before
doing anything.
This would however be a very heavy-handed approach.  First of
all, the updates are infrequent compared to simple references,
second it is not important which particular state of meta data
a consumer gets hold of, as long as it is consistent: as long
as the øCount sub{ref}ø and øT sub{ref}ø are a matching pair,
and not old enough to cause an ambiguity with hardware counter
rollover, a valid timestamp can be derived from them.
.LP
A pseudo-stable-storage with generation count method has been
chosen instead.
A ring of ten ``timehands'' data structures are used to hold the
state of the timecounter system, the periodic update function
updates the next structure with the new reference data and
scaling factor and makes it the current timehands.
.LP
The beauty of this arrangement lies in the fact that even though
a particular ``timehands'' data structure has been bumped from being
the ``currents state'' by its successor, it still contains valid data
for some amount of time into the future.
.LP
Therefore, a process which has started the timestamping process but
suffered an interrupt which resulted in the above periodic processing
can continue unaware of this afterwards and not suffer corruption
or miscalculation even though it holds no locks on the shared
meta-data.
.if t .PSPIC fig4.eps
.LP
This scheme has an inherent risk that a process may be de-scheduled for
so long time that it will not manage to complete the timestamping
process before the entire ring of timehands have been recycled.
This case is covered by each timehand having a private generation number
which is temporarily set to zero during the periodic processing, to
mark inconsistent data, and incremented to one more than the
previous value when the update has finished and the timehands
is again consistent.
.LP
The timestamping code will grab a copy of this generation number and
compare this copy to the generation in the timehands after completion
and if they differ it will restart the timestamping calculation.
.DS
.ps -1
do {
    th = timehands;
    gen = th->th_generation;
    /* calculate timestamp */
} while (gen == 0 ||
   gen != th->th_generation);
.ps +1
.DE
.LP
Each hardware device supporting timecounting is represented by a
small data structure called a timecounter, which documents the
frequency, the number of bits implemented by the counter and a method
function to read the counter.
.LP
Part of the state in the timehands structure is a pointer to the
relevant timecounter structure, this makes it possible to change
to a one piece of hardware to another ``on the fly'' by updating
the current timehands pointer in a manner similar to the periodic
update function. 
.LP
In practice this can be done with sysctl(8):
.DS
.ps -1
sysctl kern.timecounter.hardware=TSC
.ps +1
.DE
.LP
at any time while the system is running.
.SH
Suitable hardware
.PP
A closer look on ``suitable hardware'' is warranted
at this point.
It is obvious from the above description that the ideal hardware
for timecounting is a wide binary counter running at a constant
high frequency
and atomically readable by all CPUs in the system with a fast
instruction(-sequence).
.LP
When looking at the hardware support on the PC platform, one
is somewhat tempted to sigh deeply and mutter ``so much for theory'',
because none of the above parameters seems to have been on the
drawing board together yet.
.LP
All IBM PC derivatives contain a device more or less compatible
with the venerable Intel i8254 chip.
This device contains 3 counters of 16 bits each,
one of which is wired so it can interrupt the CPU when the 
programmable terminal count is reached.
.LP
The problem with this device is that it only has 8bit bus-width,
so reading a 16 bit timestamp takes 3 I/O operations: one to latch
the count in an internal register, and two to read the high and
low parts of that register respectively.
.LP
Obviously, on multi-CPU systems this cannot be done without some
kind of locking mechanism preventing the other CPUs from trying
to do the same thing at the same time.
.LP
Less obviously we find it is even worse than that:
Since a low priority kernel thread
might be reading a timestamp when an interrupt comes in, and since
the interrupt thread might also attempt to generate a timestamp,
we need to totally block interrupts out while doing those three
I/O instructions.
.LP
And just to make life even more complicated, FreeBSD uses the same
counter to provide the periodic interrupts which schedule the
\fChardclock()\fP routine, so in addition the code has to deal with the
fact that the counter does not count down from a power of two and
that an interrupt is generated right after the reloading of the
counter when it reaches zero.
.LP
Ohh, and did I mention that the interrupt rate for hardclock() will
be set to a higher frequency if profiling is active ? \**
.FS
I will not even mention the fact that it can be set also to ridiculous
high frequencies in order to be able to use the binary driven ``beep''
speaker in the PC in a PCM fashion to output ``real sounds''.
.FE
.LP
It hopefully doesn't ever get more complicated than that, but it
shows, in its own bizarre and twisted way, just how little help the
timecounter code needs from the actual hardware.
.LP
The next kind of hardware support to materialise was the ``CPU clock
counter'' called ``TSC'' in official data-sheets.
This is basically a on-CPU counter, which counts at the rate 
of the CPU clock.
.LP
Unfortunately, the electrical power needed to run a CPU is pretty
precisely proportional with the clock frequency for the
prevailing CMOS chip technology, so
the advent of computers powered by batteries prompted technologies
like APM, ACPI, SpeedStep and others which varies or throttles the
CPU clock to match computing demand in order to minimise the power
consumption \**.
.FS
This technology also found ways into stationary computers from
two different vectors.
The first vector was technical: Cheaper cooling solutions can be used
for the CPU if they are employed resulting in cheaper commodity
hardware.
The second vector was political: For reasons beyond reason, energy
conservation became an issue with personal computers, despite the fact
that practically all north American households contains 4 to 5 household
items which through inefficient designs waste more power than a 
personal computer use.
.FE
.LP
Another wiggle for the TSC is that it is not usable on multi-CPU
systems because the counter is implemented inside the CPU and
not readable from other CPUs in the system.
.LP
The counters on different CPUs are not guaranteed
to run syntonously (ie: show the same count at the same time).
For some architectures like the DEC/alpha architecture they do not even
run synchronously (ie: at the same rate) because the CPU clock frequency
is generated by a small SAW device on the chip which is very sensitive
to temperature changes.
.LP
The ACPI specification finally brings some light:
it postulates the existence of a 24 or 32 bit
counter running at a standardised constant frequency and
specifically notes that this is intended to be used for timekeeping.
.LP
The frequency chosen, 3.5795454... MHz\**
.FS
The reason for this odd-ball frequency has to be sought in the ghastly
colours offered by the original IBM PC Color Graphics Adapter:  It
delivered NTSC format output and therefore introduced the NTSC colour
sync frequency into personal computers.
.FE
 is not quite as high as one
could have wished for, but it is certainly a big improvement over
the i8254 hardware in terms of access path.
.LP
But trust it to Murphys Law: The majority of implementations so far
have failed to provide latching suitable to avoid meta-stability
problems, and several readings from the counter is necessary to
get a reliable timestamp.
In difference from the i8254 mentioned above, we do not need to
any locking while doing so, since each individual read is atomic.
.LP
An initialization routine tries to test if the ACPI counter is properly
latched by examining the width of a histogram over read delta-values.
.LP
Other architectures are similarly equipped with means for timekeeping,
but generally more carefully thought out compared to the haphazard
developments of the IBM PC architecture.
.LP
One final important wiggle of all this, is that it may not be possible
to determine which piece of hardware is best suited for clock
use until well into or even after the bootstrap process.
.LP
One example of this is the Loran-C receiver designed by Prof. Dave Mills
[MILLS1992]
which is unsuitable as timecounter until the daemon program which
implements the software-half of the receiver has properly initialised
and locked onto a Loran-C signal.
.SH
Ideal timecounter hardware
.LP
As proof of concept, a sort of an existentialist protest against
the sorry state describe above, the author undertook a project to
prove that it is possible to do better than that, since none of
the standard hardware offered a way to fully validate the timecounter
design.
.LP
Using a COTS product, ``HOT1'', from Virtual Computers Corporation
[VCC2002] containing a FPGA chip on a PCI form factor card, a 26
bit timecounter running at 100MHz was successfully implemented.
.LP
.if t .PSPIC fig5.eps
.LP
.LP
In order to show that timestamping does not necessarily have to
be done using unpredictable and uncalibratable interrupts, an
array of latches were implemented as well, which allow up to 10
external signals to latch the reading of the counter when
an external PPS signal transitions from logic high to logic
low or vice versa.
.LP
Using this setup, a standard 133 MHz Pentium based PC is able to
timestamp the PPS output of the Motorola UT+ GPS receiver with
a precision of \(+- 10 nanoseconds \(+- one count which in practice
averages out to roughly \(+- 15 nanoseconds\**:
.FS
The reason the plot does not show a very distinct 10 nanosecond
quantization is that the GPS receiver produces the PPS signal from
a clock with a roughly 55 nanosecond period and then predicts in
the serial data stream how many nanoseconds this will be offset
from the ideal time.
This plot shows the timestamps corrected for this ``negative
sawtooth correction''.
.FE
.LP
.if t .PSPIC gps.ps
.LP
It shold be noted that the author is no hardware wizard and
a number of issues in the implementation results in less than
ideal noise performance.
.LP
Now compare this to ``ideal'' timecounter to the normal setup
where the PPS signal is used
to trigger an interrupt via the DCD pin on a serial port, and
the interrupt handler calls \fCnanotime()\fP to timestamp
the external event \**:
.FS
In both cases, the computers clock frequency controlled
with a Rubidium Frequency standard.
The average quality of crystals used for computers would
totally obscure the curves due to their temperature coefficient.
.FE
.LP
.if t .PSPIC intr.ps
.LP
It is painfully obvious that the interrupt latency is the
dominant noise factor in PPS timestamping in the second case.
The asymetric distribution of the noise in the second plot
also more or less entirely invalidates the design assumption
in the NTP PLL/FLL kernel code that timestamps are dominated
by gaussian noise with few spikes.
.SH
Status and availability
.PP
The timecounter code has been developed and used in FreeBSD
for a number of years and has now reached maturity.
The source-code is located almost entirely in the kernel source file
kern_tc.c, with a few necessary adaptations in code which
interfaces to it, primarily the NTP PLL/FLL code.
.LP
The code runs on all FreeBSD platforms including i386, alpha,
PC98, sparc64, ia64 and s/390 and contains no wordsize or
endianess issues not specifically handled in the sourcecode.
.LP
The timecounter implementation is distributed under the ``BSD''
open source license or the even more free ``Beer-ware'' license.
.LP
While the ability to accurately model and compensate for
inaccuracies typical of atomic frequency standards are not
catering to the larger userbase, but this ability and precision
of the code guarntees solid support for the widespread deployment
of NTP as a time synchronization protocol, without rounding
or accumulative errors.
.LP
Adding support for new hardware and platforms have been
done several times by other developers without any input from the
author, so this particular aspect of timecounters design
seems to work very well.
.SH
Future work
.PP
At this point in time, no specific plans exist for further
development of the timecounters code.
.LP
Various micro-optimizations, mostly to compensate for inadequate
compiler optimization could be contemplated, but the author
resists these on the basis that they significantly decrease
the readability of the source code.
.SH
Acknowledgements
.PP
.EQ
delim ññ
.EN
The author would like to thank:
.LP
Bruce Evans
for his invaluable assistance
in taming the evil i8254 timecounter, as well as the enthusiastic
resistance he has provided throughout.
.PP
Professor Dave Mills of University of Delaware for his work on
NTP, for lending out the neglected twin Loran-C receiver and for
picking up the glove when timecounters made it clear
that the old ``microkernel'' NTP timekeeping code were not up to snuff
[MILLS2000B].
.PP
Tom Van Baak for helping out, despite the best efforts of the National
Danish Posts center for Customs and Dues to prevent it.
.PP
Corby Dawson for helping with the care and feeding for caesium standards.
.PP
The staff at the NELS Loran-C control station in Bø, Norway for providing
information about step-changes.
.PP
The staff at NELS Loran-C station Eiðe, Faeroe
Islands for permission to tour their installation.
.PP
The FreeBSD users for putting up with ``micro uptime went backwards''.
.SH
References
.LP
[AG2002]
Published specifications for Agilent model 5071A Primary Frequency
Standard on 
.br
http://www.agilent.com
.LP
[DMK2001]
"Accuracy Evaluation of a Cesium Fountain Primary Frequency Standard at NIST."
D. M. Meekhof, S. R. Jefferts, M. Stephanovic, and T. E. Parker
IEEE Transactions on instrumentation and measurement, VOL. 50, NO. 2,
APRIL 2001.
.LP
[PHK2001]
"Monitoring Natural Gas Usage"
Poul-Henning Kamp
http://phk.freebsd.dk/Gasdims/
.LP
[MILLS1992]
"A computer-controlled LORAN-C receiver for precision timekeeping."
Mills, D.L. 
Electrical Engineering Department Report 92-3-1, University of Delaware, March 1992, 63 pp.
.LP
[MILLS2000A]
Levine, J., and D. Mills. "Using the Network Time Protocol to transmit International Atomic Time (TAI)". Proc. Precision Time and Time Interval (PTTI) Applications and Planning Meeting (Reston VA, November 2000), 431-439.
.LP
[MILLS2000B]
"The nanokernel."
Mills, D.L., and P.-H. Kamp.
Proc. Precision Time and Time Interval (PTTI) Applications and Planning Meeting (Reston VA, November 2000), 423-430.
.LP
[RGO2002]
For an introduction to Harrison and his clocks, see for
instance 
.br
http://www.rog.nmm.ac.uk/museum/harrison/
.br
or for
a more detailed and possibly better researched account: Dava
Sobels excellent book, "Longitude: The True Story of a Lone
Genius Who Solved the Greatest Scientific Problem of His
Time" Penguin USA (Paper); ISBN: 0140258795.
.LP
[SAGE]
This ``gee-wiz'' kind of article in Dr. Dobbs Journal is a good place to
start:
.br
http://www.ddj.com/documents/s=1493/ddj0001hc/0085a.htm
.LP
[VCC2002]
Please consult Virtual Computer Corporations homepage:
.br
http://www.vcc.com
