import os
import sys
import time
import signal
import threading
from force_rc_error import pywhere
cpu_timer_signal = signal.ITIMER_REAL
cpu_signal = signal.SIGALRM
cpu_sampling_rate = 0.01

def compute_frames_to_record():
    """Collect all stack frames that Scalene actually processes."""
    frames = [
        (
                sys._current_frames().get(t.ident),

            t.ident,
        )
        for t in threading.enumerate()
        if t != threading.main_thread()
    ]
    # Put the main thread in the front.

    tid =  threading.main_thread().ident
    frames.insert(
        0,
        (
            sys._current_frames().get(tid, None),
            tid,
        ),
    )
    new_frames = []
    for (frame, tident) in frames:
        orig_frame = frame
        if not frame:
            continue
        fname = frame.f_code.co_filename
        if not fname:
            back = frame.f_back
            fname = back.f_code.co_filename
        while frame.f_back is not None: 

            if frame:
                new_frames.append((frame, tident, orig_frame))
                frame =  frame.f_back
    del frames[:]

    return new_frames

def process_cpu_sample(
        _signum,
        new_frames,
        now_virtual: float,
        now_wallclock: float,
        now_sys: float,
        now_user: float,

    ) -> None:
        """Handle interrupts for CPU profiling."""

        main_thread_frame = new_frames[0][0]
        for (frame, tident, orig_frame) in new_frames:
            if frame == main_thread_frame:
                continue
            enter_function_meta(frame, None)
        # Clean up all the frames
        del new_frames[:]
        del new_frames
        # Scalene.__stats.total_cpu_samples += total_time

def memcpy_signal_handler(
    signum,
    this_frame,
) -> None:
    """Handle memcpy signals."""
    # Scalene.__memcpy_sigq.put((signum, this_frame))
    print("MEMCPY")
    del this_frame

def cpu_signal_handler(
    signum,
    this_frame,
) -> None:
    """Handle CPU signals."""
    # Get current time stats.
    now_sys: float = 0
    now_user: float = 0

    time_info = os.times()
    now_sys = time_info.system
    now_user = time_info.user
    now_virtual = time.process_time()
    now_wallclock = time.perf_counter()
    print("PING")
    # Process this CPU sample.
    process_cpu_sample(
        signum,
        compute_frames_to_record(),
        now_virtual,
        now_wallclock,
        now_sys,
        now_user
    )
    # Store the latest values as the previously recorded values.
def enter_function_meta(
        frame, stats
) -> None:
    """Update tracking info so we can correctly report line number info later."""
    fname = frame.f_code.co_filename
    lineno = frame.f_lineno

    f = frame
    try:
        while "<" in f.f_code.co_name:
            f = f.f_back
            if f is None:
                return
    except Exception:
        return

    fn_name = f.f_code.co_name
    firstline = f.f_code.co_firstlineno
    while (
        f
        and f.f_back
        and f.f_back.f_code

    ):
        if "self" in f.f_locals:
            break
        if "cls" in f.f_locals:
            break
        f = f.f_back
def malloc_signal_handler(
    signum,
    this_frame,
) -> None:
    """Handle allocation signals."""
    if this_frame:
        enter_function_meta(this_frame, None)
    # Walk the stack till we find a line of code in a file we are tracing.
    found_frame = False
    f = this_frame
    while f and f.f_back is not None:
        f = f.f_back
    if not f: 
        return
    assert f
    del this_frame

def free_signal_handler(
    signum,
    this_frame,
) -> None:
    """Handle free signals."""
    # if this_frame:
    #     Scalene.enter_function_meta(this_frame, Scalene.__stats)
    # Scalene.__alloc_sigq.put([0])
    del this_frame


import numpy as np

def main1():
    # Before optimization
    x = np.array(range(10**7))
    y = np.array(np.random.uniform(0, 100, size=10**8))

def main2():
    # After optimization, spurious `np.array` removed.
    x = np.array(range(10**7))
    y = np.random.uniform(0, 100, size=10**8)


def main():
    pywhere.placeholder()
    signal.signal(cpu_signal, cpu_signal_handler)
    signal.setitimer(
        cpu_timer_signal,
        cpu_sampling_rate,
    )
    signal.signal(signal.SIGPROF, memcpy_signal_handler)
    signal.signal(signal.SIGXCPU, malloc_signal_handler)
    signal.signal(signal.SIGXFSZ, free_signal_handler)
    main1()
    main2()


