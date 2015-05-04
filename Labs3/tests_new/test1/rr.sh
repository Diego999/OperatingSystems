#!/bin/sh

echo 20 > /proc/sys/kernel/sched_dummy_timeslice
echo 20 > /proc/sys/kernel/sched_dummy_age_threshold

nice -n 12 ../loop/loop A &
nice -n 12 ../loop/loop B &
nice -n 12 ../loop/loop C &

wait
echo 'done'

