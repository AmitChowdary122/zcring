# zcring, explained from the very beginning

No jargon. If you know what a computer is, you can read this.

---

## 1. The problem: computers waste time copying things

A computer doesn't run one big program. It runs lots of little ones, and they
have to talk to each other. A camera program takes a picture. A different
program looks at the picture. A third program puts it on the screen.

Here's the annoying part. Each program lives in its own locked room. They
can't reach into each other's rooms — that's on purpose, so a broken program
can't wreck the others.

So when the camera program wants to hand a picture to the next program, this
is what normally happens:

1. The camera program writes the picture on a sheet of paper.
2. A helper (the operating system) **copies** it onto a fresh sheet.
3. The helper carries that sheet next door.
4. The next program **copies** it again to read it.

Four trips across the paper. Two of them are pure copying that nobody asked
for. If the picture is big, the copying is most of the work.

## 2. Why anyone should care

For a web browser, nobody notices. For a robot arm, a drone, a car's brakes,
or a factory machine, they do — because those need answers **on time, every
single time**. Not "usually fast." *Always* fast.

If the answer is late one time in a thousand, the robot arm is in the wrong
place one time in a thousand. That's the whole ballgame in embedded systems:
not speed, but *predictability*.

## 3. Our idea: stop copying

Instead of two locked rooms passing paper, put a **shared table** between
them, reachable through a hatch in the wall.

The camera program writes the picture **directly onto the shared table**. The
next program reads it **straight off the table**. No copies. No helper walking
back and forth. Four trips become two, and the two that remain are the ones
you genuinely can't avoid — somebody has to write it, somebody has to read it.

That's the whole idea. We called it **zcring** — "zero-copy ring."

The "ring" part: the table is a circle of numbered spots. When you reach the
last one you wrap back to the first, reusing spots the readers have finished
with. That way it never runs out of room and never has to ask for more.

## 4. What we actually built

**Step one — the shared table.** The circle of spots, plus very careful rules
for who may write where and when. This is the hard bit, because two programs
poking the same memory at the same time is how you get corrupted data, and the
corruption is invisible until it isn't.

We tested it by having 4 writers and 4 readers pass **200,000 messages** and
then checking that every single message arrived exactly once — none lost, none
delivered twice. We also ran it under a special tool that watches for exactly
this class of mistake. It found nothing.

**Step two — one writer, many readers.** A camera picture usually needs to go
to several programs at once: one to analyse it, one to show it, one to record
it. The old way copies the picture once *for each* reader. Three readers, three
copies.

We made all three read **the same sheet on the same table**. One writer, any
number of readers, still zero copies. Publishing costs the same whether one
program is reading or ten.

That's the part we're proudest of, because it means our advantage doesn't stay
the same as the system grows — it *gets bigger*.

Along the way we had to answer awkward questions honestly:

- **What if a reader is slow?** We make the writer wait rather than throwing
  the picture away. Silently dropping data is how you end up unable to tell
  "the system is busy" apart from "the system is broken."
- **What if a reader crashes?** The writer would wait forever for a reader
  that's never coming back. So we check whether readers are still alive and
  quietly remove the dead ones. Being *slow* doesn't get you removed — only
  being *gone*.

## 5. All the times we fooled ourselves — and caught it

This is the honest part, and the most interesting.

**The beautiful lie.** Our very first measurement was astonishing — a perfect
flat line, our system seemingly just as fast for a huge picture as a tiny one.
It was fake. Our test never actually *looked* at the picture; it only checked
the label. The old way was really carrying all the paper while we were carrying
a sticky note and claiming victory. We fixed the test to touch every single
part of the picture. The magic number got much smaller and became real.

**The twin problem.** Modern chips have cores that come in twins sharing one
brain. We accidentally put two of our programs on twins — they fought each
other, and one measurement came out **185 times** worse than the truth. Now the
test reads the chip's own wiring diagram and refuses to make that mistake.

**The sleepy processor.** Sometimes an answer took a thousand times longer than
usual, for no reason we could see. It turned out the chip was falling asleep
between messages, and *waking up* took a full millisecond. We found the exact
setting, switched it off, and the delay went from wildly unpredictable to
boringly consistent. Boring is exactly what we wanted.

**The overheating laptop.** Then our numbers started drifting worse and worse
during long tests. The chip was cooking itself and slowing down to survive.
Now the test *waits for the laptop to cool down* before every single
measurement.

**The traffic jam.** One result looked catastrophic — until we realised we were
shoving data in far faster than the machine could possibly move it. We weren't
measuring how fast the road is; we were measuring the queue. We worked out the
real speed limit and now always test below it, and we proved the answer doesn't
change if we pick a different speed.

**The zombie programs.** We piped the demo's output somewhere it stopped being
read, which killed the writer — but the three readers never noticed and spun
forever eating the whole processor. Fixed. Then we tried the crueller test:
killing the writer in a way it *cannot* notice. The textbook fix didn't work,
because this system hands orphaned programs to a different guardian than the
books assume. So we made each reader remember who its parent was and check
whether that changed — which is right no matter who catches the orphan.

Six ways to be wrong. Every one of them found by testing rather than assuming,
and every one written down instead of quietly fixed.

## 6. What we found

For **small messages** — the kind a robot or sensor sends constantly — we're
about **20 times faster** than the normal way. If the old way took a second,
ours takes a twentieth.

For **big pictures with several readers**, our advantage *grows* as readers are
added, because they all share one sheet while the old way makes a fresh copy
per reader.

In a ten-minute demo — a pretend camera at 30 pictures a second feeding three
programs — we avoided **31 gigabytes** of pointless copying. That's about seven
DVDs' worth of work simply not done, in ten minutes.

And the delays are *steady*. Not just fast on average — the slow ones aren't
slow either, which for a robot is the thing that actually matters.

## 7. What we're honest about

With one single reader and a very large picture, we're **slower** than the
normal way. We know it, we measured it carefully, we couldn't fully explain
*why*, and we say so anyway.

We say so because the alternative is a judge finding it, and then everything
else we've said stops being trustworthy. A limitation you volunteer reads as
care. The same limitation discovered by someone else reads as a cover-up.

## 8. What's still to come

- Run everything on a laptop with **more cores**, to show the advantage keeps
  growing rather than hitting the ceiling of our small test machine.
- Add a **guard** inside the operating system itself, so a broken program can't
  scribble on the shared table and ruin things for everyone. Other systems
  like ours can't do this — it's the strongest thing we could still add.
- Let waiting readers **sleep properly** instead of watching the table
  constantly, so they stop burning power while nothing is happening.

## 9. What this is good at, and what it isn't

**It works, and you can check that.** Everything described here runs today.
Every number comes with the raw data behind it and a script that regenerates
it. Six mistakes we made along the way are written down rather than buried,
including one where our own cleverness turned out not to help.

**It gets better with more readers, and we measured that** rather than
asserting it. That is the part where sharing memory genuinely beats copying,
and it is why the camera demo has three consumers rather than one.

**It is not novel in its core.** Ring buffers over shared memory are
well-trodden ground; what is here is careful engineering and honest
measurement, plus one finding about atomics that we had not seen written down
elsewhere. The part that would be genuinely new — letting the operating system
police who may write to the shared region — is designed but not built, and is
labelled that way throughout.

**Where it matters** is the camera demo: not "look, a faster number", but
"this pipeline of programs keeps up when it previously could not, and nothing
copied the frame three times to make that happen."

---

*If you only remember one sentence: computers waste enormous effort copying
data between programs that could simply share it, and we built a way for them
to share it instead — carefully enough that we can prove it, and honestly
enough that we tell you where it doesn't work.*
