
README
------
WebMond is an advanced unix/linux system and program monitoring tool.

Built on top of HyperMond is a web monitoring command 'webmon' that allows you
to pipe the output of HyperMond to an html rendered page and then use that to
constantly monitor your process stats.


AUTHORS
-------
* Douglas Brandt
* Kerry S.


NOTES
-----
Completion Status: This program is fully functional.

* We have one command thread (which is our main), one system thread that can be
  turned on or off, and 10 monitoring can be spawned off to monitor
  processes/executables.  We don't limit the lifetime number of threads to 10.
  When monitor threads end, additional monitor threads may be created.
* When exiting, it may take a while as it waits for each thread to die.  This
  is expected due to the fact that the thread may be in a sleep/waiting state
  for a long duration (as set by the user through the -i flag).  These
  characteristics will also occur with the remove and kill commands.
* The timestamp is displayed in unix format.

Tested on Ubuntu 12.04:

Get dependencies:

* sudo apt-get install libreadline-dev
