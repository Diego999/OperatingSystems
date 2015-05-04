#!/bin/sh

echo 12 > /proc/sys/kernel/sched_dummy_timeslice
echo 36 > /proc/sys/kernel/sched_dummy_age_threshold

nice -n 11 ../loop/loop A &
nice -n 12 ../loop/loop B &
nice -n 13 ../loop/loop C &

wait
echo 'done'

