# The-Pulse

This repository holds the core 'render' program that runs on the Bela low-latency embedded computng platform, built upon the Beaglebone Black ARM Cortex-A8

The Pulse was a project I developed as part of a funded post-graduate internship that I secured at The University of the West of England (UWE), specifically the enterprise studio - Impulse.
The Pulse is an embedded beat-detection unit that uses physical data to detect the tempo that a drummer is playing at and synchronize any connected electronics via MIDI. This makes it possible to include sequencers, synths, drum machines etc. in dynamic and expressive live performances, making the machines slave to your timing rather than you being slave to theirs.

To learn more about the Pulse with diagrams, pictures and videos, please visit its information page on my website - https://www.newresmedia.com/the-pulse 

The code here is the C++ program that runs continuously on the Bela that executes the monitoring of sensor data (onset detection), beat tracking algorithm and Midi output (in order to slave connected Midi devices to the drummer).
