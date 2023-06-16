# Elements:
- C++ server program to receive requests to serve data and then to send via UDP.
- Python program to request data then display per requirements from Rocket Lab

# Prerequisites:
- g++11 or higher is required for std::counting_semaphore (or specialization std::binary_semaphore used in this program) 
  and maybe some other features.
- Has been run and tested on Ubuntu 20.04 and Windows subsystem for Linux (see note about this below in known issues)
- Python 3 - uses matplotlib and pyqt5 (requirement from Rocked Lab).  Justification for matplotlib: the standard Python plotting library.

# Build notes:
## C++ data server
1. Clone this project into a working directory on a Linux machine.
2. Open terminal/console and cd into the working directory.
3. Run make:  
    **$make**
   - Depending on your system setup, you may need to change the C++ compiler to
     or from "g++" / "g++-11"
4. Verify that the executable file ./server_sensor_data has been created.

## Python data client
1. Either clone the repository into a working directory on the same or a separate machine, or if
   you are working entirely on the same system, then plan to use the same directory as
   for the C++ program
2. cd into the working directory
3. Follow the directions to create the correct python environment in EnvironmentCreation.txt 

# Execution
## Prep
1. Identify the IP address of the C++ server machine using ifconfig or other utility.
2. Verify that you have a valid local network connection between the C++ server machine and the
   Python machine; ping might work.

## C++ data server
1. In the working directory you created for the C++ program, open a terminal and
  execute 
  **$./server_sensor_data**
2. Look for some startup activity in the console
3. If the program fails with a comment about "bind failed", then another program (most likely a prior
   instance of this program) is still running and controls the port.  Kill the port as follows:
   -- Identify the process that owns the port: **sudo netstat -tulpn | grep 8080**
   -- Kill the process with prejudice: **sudo kill -9 [PID]**
   -- Verify that the process is no longer running: **sudo netstat -tulpn | grep 8080**
   -- Start the program again as in step 1.

## Python client
1. In the working directory for the python program, open and terminal and
  execute the startup command:  
  **$python3 qt_program.py [remote_address] [remote_port] [duration_seconds] [period_milliseconds]**
    
    Example:
    
    **$python3 qt_program.py 192.168.0.105 8080 10 100**
    [For now, 8080 is the only acceptable port ID; see notes below.]
2. After the UI starts, click on these buttons:
  - Request ID - You'll see output in both consoles
  - Send Start Message - The python graph UI will display 2 waveforms, and you'll see output in both consoles, indicating data traffic.
  - Send Stop Message - Data traffic will stop.
  - Save to PDF - The current plot and text is saved to ./plot.pdf
3. Try starting and stopping repeatedly
4. When you are done, click on the Python close button.
5. Note that the Rocket Lab requirements ask for "rate in milliseconds", which is ambiguous.  This program specifies period in milliseconds
   rather than sample rate.
   
# Console output
- Both programs are fairly verbose to stdout. In a production system this wouldn't be the case.

# Limitations and bugs
- The programs work 1-to-1; this version does not include N-to-N or N-to-1 operation.
- The python program implements port number selection, but the C++ program is hard-coded to 8080, so
  the user must select 8080 as the port number.
- The network behavior, in particular timeouts, works differently on Windows subsystem for Linux; the program
  runs there, but might exhibit some different behavior during start and stop.
- Stopping and restarting in the Python UI produces a temporary artifact in the graph.
- If the duration of the data set expires before a stop command, re-starting will require multiple
  clicks on the start button [Restarting after a stop is not in the requirements from Rocket Lab]
- Program doesn't display any license info,e.g. for Qt.
- Program sends 2 status idle messages when data stops sending, rather than 1.
# Other comments:
 - There are numerous comments labeled "TODO", showing some work that would be continued.
 - ncat can also be used to test the C++ data server.

# Architecture and use of other libraries and systems
## C++
The C++ data server uses generic C++ features and requires no tools or packages beyond the
dev tools that accompany g++ 11 or higher.  It runs 2 threads: one to send data and the other
to receive and dispatch messages from the client.
## Python program
The starting point for the python program is an example program found here: https://www.pythonguis.com/tutorials/plotting-matplotlib/,
which plotted random data. It provided a good example of the matplotlib/qt integration.
