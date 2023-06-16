import socket
import re
import threading
import time
from time import sleep
import os


import sys
import random
import matplotlib
matplotlib.use('Qt5Agg')
from PyQt5 import QtCore, QtWidgets
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure
from PyQt5.QtWidgets import QApplication, QWidget, QMainWindow, QPushButton
from PyQt5.QtCore import QSize, Qt

import matplotlib.pyplot as plt





class MplCanvas(FigureCanvas):

    def __init__(self, parent=None, width=5, height=4, dpi=100):
        fig = Figure(figsize=(width, height), dpi=dpi)
        self.fig = fig
        self.axes = fig.add_subplot(111)
        super(MplCanvas, self).__init__(fig)

class MainWindow(QtWidgets.QMainWindow):

    def __init__(self, *args, **kwargs):
        super(MainWindow, self).__init__(*args, **kwargs)

        self.setWindowTitle("Sensor Simulator")

        self.button_start = QPushButton("Send start message")
        self.button_stop = QPushButton("Send stop message")
        self.button_request_id = QPushButton("Request ID")
        self.button_save_to_pdf = QPushButton("Save to PDF")
        # TODO KJC Future version enter this with text field and button
        self.filename = "plot.pdf"

        # TODO KJC Future version fill in with text field and button
        self.seconds_duration = seconds_duration
        self.milliseconds_rate = milliseconds_rate
        self.server_socket = server_socket
        self.remote_address = remote_address
        self.local_address = local_address

  
        def send_start_message_local():
            send_start_message(self.seconds_duration, self.milliseconds_rate, self.server_socket, self.remote_address)

        def send_stop_message_local():
            send_stop_message(self.server_socket, self.remote_address)

        def send_id_message_local():
            send_id_message(self.server_socket, self.remote_address)
        def save_plot_to_pdf():
            if(os.path.isfile(self.filename)):
                os.remove(self.filename)
            self.canvas.fig.savefig(self.filename, format="pdf", bbox_inches = "tight")

        self.button_start.clicked.connect(send_start_message_local)
        self.button_stop.clicked.connect(send_stop_message_local)
        self.button_request_id.clicked.connect(send_id_message_local)
        self.button_save_to_pdf.clicked.connect(save_plot_to_pdf)

        


        self.canvas = MplCanvas(self, width=5, height=4, dpi=100)
        self.activity_string = "Recording values from sensor.\n"
        self.sensor_id_string = "Model and serial number N/A. Send ID; command\n"
        self.location_string = f"Address and port of the sensor: {remote_address}\n"
        self.test_length_string = f"Test duration: {self.seconds_duration} seconds\n"
        self.test_rate_string = f"Test data rate: {self.milliseconds_rate} milliseconds\n"
        self.description_string = self.activity_string + self.sensor_id_string + self.location_string + \
                                  self.test_length_string + self.test_rate_string

        self.text = self.canvas.fig.text(0, 0, self.description_string)
        self.canvas.fig.subplots_adjust(bottom = 0.25)

        n_data = 100
        self.n_data = 100
        self.current_data = 0
        self.xdata = []
        self.y_millivolts = []
        self.y_milliamps = []

        # We need to store a reference to the plotted line
        # somewhere, so we can apply the new data to it.
        self.line2D_millivolts_reference = None
        self.line2D_milliamps_reference = None

        self.show()

        self.main_widget = QtWidgets.QWidget(self)
        self.main_widget.setFocus()
        self.setCentralWidget(self.main_widget)

        layout = QtWidgets.QVBoxLayout(self.main_widget)

        layout.addWidget(self.button_request_id)
        layout.addWidget(self.button_start)     
        layout.addWidget(self.button_stop)   
        layout.addWidget(self.button_save_to_pdf)
        layout.addWidget(self.canvas)

        self.setMinimumSize(QSize(700,700))

    # TODO KJC update_plot should remake the graph when a message restarts.
    # TODO KJC For a future version. Some complications in removing the 
    # Matplotlib widget and adding back as the rest of the objects in the Window
    # take up all the layout space and size of new plot is zero. Figure this out
    # for future version.
    def update_plot(self, value_millivolts, value_milliamps, timestamp):
        if self.current_data < self.n_data:
            self.current_data = self.current_data + 1
            self.y_millivolts = self.y_millivolts + [value_millivolts]
            self.y_milliamps = self.y_milliamps + [value_milliamps]
            self.xdata = self.xdata + [timestamp]
        else :
            # Drop off the first y element, append a new one. Do same for timestamp.
            self.y_millivolts = self.y_millivolts[1:] + [value_millivolts]
            self.y_milliamps = self.y_milliamps[1:] + [value_milliamps]
            self.xdata = self.xdata[1:] + [timestamp]

        # Don't need to clear the axis anymore
        if self.line2D_millivolts_reference is None and self.line2D_milliamps_reference is None:
            # We don't have a plot objects yet. The plot method returns us list of 2D lines
            # For each of the separate plots in the figure
            line2D_objects = self.canvas.axes.plot(self.xdata, self.y_millivolts, self.y_milliamps, 'r')
            self.line2D_millivolts_reference = line2D_objects[0]
            self.line2D_milliamps_reference = line2D_objects[1]
        else:
            # Use our reference to 2DLine objects in the plot to update the graph
            self.line2D_millivolts_reference.set_ydata(self.y_millivolts)
            self.line2D_millivolts_reference.set_xdata(self.xdata)
            self.line2D_milliamps_reference.set_ydata(self.y_milliamps)
            self.line2D_milliamps_reference.set_xdata(self.xdata)

        # Scale the axes as the range of the data changes over the course of the measurement
        self.canvas.axes.relim()
        self.canvas.axes.autoscale_view(True,True,True)
        # Remember to redraw
        self.canvas.draw()

    def update_displayed_model_and_serial(self, model, serial):
        print("In update display of ID function")
        self.sensor_id_string = f"Model: {model} - Serial: {serial}\n"
        self.description_string = self.activity_string + self.sensor_id_string + self.location_string + \
                                  self.test_length_string + self.test_rate_string
        self.text.remove()
        self.text = self.canvas.fig.text(0, 0, self.description_string)
        self.canvas.fig.subplots_adjust(bottom = 0.25)
        self.canvas.draw()










# Messages we send
discovery_message = "ID;"
stop_message = "TEST;CMD=STOP;"

# Messages we receive
test_started_message = "TEST;RESULT=STARTED;"
test_stopped_message = "TEST;RESULT=STOPPED;"
test_already_started_message = "TEST;RESULT=error;MSG=already_started;"
test_already_stopped_message = "TEST;RESULT=error;MSG=already_stopped;"
idle_message = "STATUS;STATE=IDLE;"

# Regex strings
raw_string_match_discovery = r"^ID;MODEL=([0-9]+);SERIAL=([0-9]+);$"
raw_string_match_data = r"^STATUS;TIME=([0-9]+);MV=(-?[0-9]+);MA=(-?[0-9]+);$"

def capture_fields(raw_regex_string, input_string):
    captures = re.search(raw_regex_string, input_string)
    # None is our non matching string return type
    if(captures is None):
        return None
    groups = captures.groups()
    return groups

def test_regex_capture(raw_regex_string, num_captures, input, captures_array):
    for j, test_string in enumerate(input):
        groups = capture_fields(raw_regex_string, test_string)
        print(groups)
        
        for i in range(num_captures):
            if(not (groups[i] == captures_array[j][i]) ):
                print(groups[i] + " not equal to " + captures_array[j][i])
                return False
    return True

def log_incorrect_message(incorrect_message):
    pass

def read_data_message(message):
    capture_result = capture_fields(raw_string_match_data, message)
    if(capture_result is None):
        log_incorrect_message(message)
    return capture_result

def read_identification_message(message):
    # TODO KJC remove print
    print(message)
    capture_result = capture_fields(raw_string_match_discovery, message)
    if(capture_result is None):
        log_incorrect_message(message)
    # TODO KJC remove print
    print(message)
    return capture_result

def send_start_message(s, ms, server_socket, address):
    start_message = f"TEST;CMD=START;DURATION={s};RATE={ms};"
    # TODO KJC check for errors
    return_val = server_socket.sendto(start_message.encode('latin-1'), address)

def send_stop_message(server_socket, address):
    # TODO KJC check for errors
    return_val = server_socket.sendto(stop_message.encode('latin-1'), address)

def send_id_message(server_socket, address):
    # TODO KJC check for errors
    return_val = server_socket.sendto(discovery_message.encode('latin-1'), address)


def process_data_message(capture_result, window):
    print("In process_data_message()")
    milliseconds = int(capture_result[0])
    millivolts = int(capture_result[1])
    milliamps = int(capture_result[2])
    print(milliseconds, millivolts, milliamps)
    window.update_plot(millivolts, milliamps, milliseconds)

def process_identification_message(capture_result, window):
    print("In process_identification_message()")
    model = capture_result[0]
    serial = capture_result[1]
    print(model, serial)
    window.update_displayed_model_and_serial(model, serial)

def process_idle_message():
    print("In process_idle_message()")

def process_started_message():
    print("In process_started_message()")

def process_stopped_message():
    print("In process_stopped_message()")

def process_error_state_message(message):
    print("In process_error_state_message()")
    print("Received: " , message)

def process_unrecognized_message(message):
    print("In process_unrecognized_message()")
    print("Received: " , message)


index_first = 0
index_second = 0
first_list = []
second_list = []
threshold_data = 20
seconds = 0
milliseconds = 0


def process_messages(server_socket, window):
    process_messages.number_consecutive_socket_errors = 0
    while True:
        try:
            message, address = server_socket.recvfrom(1024)
            message = message.decode('latin-1')
        except socket.error as e:
            print("Encountered an error while calling recvfrom:")
            print(repr(e))
            process_messages.number_consecutive_socket_errors = process_messages.number_consecutive_socket_errors + 1
            if(process_messages.number_consecutive_socket_errors > 10):
                print("More than ten consecutive recvfrom failures. Something looks wrong.")
                sleep(10)
            if(process_messages.number_consecutive_socket_errors >= 3):
                sleep(3)
            continue

        process_messages.number_consecutive_socket_errors = 0
        #print(message)
        capture_result_data = read_data_message(message)
        if capture_result_data is not None:
            process_data_message(capture_result_data, window)
        else:
            capture_result_identification = read_identification_message(message)
            if(capture_result_identification is not None):
                process_identification_message(capture_result_identification, window)
            elif(message == test_started_message):
                process_started_message()
            elif(message == test_stopped_message):
                process_stopped_message()
            elif(message == test_already_started_message or 
                message == test_already_stopped_message):
                process_error_state_message(message)
            elif(message == idle_message):
                process_idle_message()
            else:
                process_unrecognized_message(message)

#############################################

# Some test programs for the parsing
def run_tests():
    discovery_response_test_string = []
    discovery_response_test_string_captures = []

    discovery_response_test_string.append("ID;MODEL=15;SERIAL=4643;") 
    discovery_response_test_string_captures.append(["15", "4643"])

    discovery_response_test_string.append("ID;MODEL=76576;SERIAL=07485;")
    discovery_response_test_string_captures.append(["76576", "07485"])


    # Test ID string parsing for parsing correctness
    if(not test_regex_capture(raw_string_match_discovery, 2, discovery_response_test_string, discovery_response_test_string_captures)):
        print("Failed regex tests for discovery strings")
        exit()


    #STATUS;TIME=ms;MV=mv;MA=ma;
    data_string_test = []
    data_string_test_captures = []

    data_string_test.append("STATUS;TIME=542;MV=-345;MA=54;")
    data_string_test_captures.append(["542", "-345", "54"])

    data_string_test.append("STATUS;TIME=6542;MV=345434;MA=2434054;")
    data_string_test_captures.append(["6542", "345434", "2434054"])

    data_string_test.append("STATUS;TIME=23500;MV=-345342;MA=34000;")
    data_string_test_captures.append(["23500", "-345342", "34000"])

    data_string_test.append("STATUS;TIME=34545;MV=-999;MA=-1553;")
    data_string_test_captures.append(["34545", "-999", "-1553"])

    data_string_test.append("STATUS;TIME=3653;MV=-554;MA=-6;")
    data_string_test_captures.append(["3653", "-554", "-6"])

    data_string_test.append("STATUS;TIME=4634;MV=-87;MA=-3433;")
    data_string_test_captures.append(["4634", "-87", "-3433"])

    data_string_test.append("STATUS;TIME=3454;MV=-36;MA=-434;")
    data_string_test_captures.append(["3454", "-36", "-434"])

    data_string_test.append("STATUS;TIME=565;MV=-4584;MA=-0;")
    data_string_test_captures.append(["565", "-4584", "-0"])

    data_string_test.append("STATUS;TIME=200;MV=1111;MA=-4432;")
    data_string_test_captures.append(["200", "1111", "-4432"])

    data_string_test.append("STATUS;TIME=150;MV=797;MA=5;")
    data_string_test_captures.append(["150", "797", "5"])


    # Test data strings for correctness matching
    if(not test_regex_capture(raw_string_match_data, 3, data_string_test, data_string_test_captures)):
        print("Failed regex tests for data strings")
        exit()



    # Test None return when have matching string with nonmatching extra characters on the end
    print("What do you get when you have a match with garbage at the end?")
    return_value = re.search(raw_string_match_data, "STATUS;TIME=150;MV=797;MA=5;agsd")
    if(not return_value is None):
        print("Matched with an incorrect string")
        exit()
    print("Above is what match with garbage at the end return value looks like")





###########################################################


# python qt_program.py 192.168.0.105 8080 100 250
if(len(sys.argv) != 5):
    print("Usage: python qt_program.py remote_address remote_port duration_seconds rate_milliseconds")
    print(f"Wrong number of parameters: Expect 6. Got {len(sys.argv)}")
    exit()


if not ((sys.argv[2]).isdigit() and (sys.argv[3]).isdigit() and (sys.argv[4]).isdigit()) :
   print("Usage: python qt_program.py [remote_address] [remote_port] [duration_seconds] [rate_milliseconds]")
   print("Usage: remote_port, duration_seconds, rate_milliseconds must be positive integers")

print(f"Got parameters: {sys.argv[1]}, {sys.argv[2]}, {sys.argv[3]}, {sys.argv[4]}")

remote_address = (sys.argv[1], int(sys.argv[2]))
seconds_duration = int(sys.argv[3])
milliseconds_rate = int(sys.argv[4])


server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

local_address = ('', 8082)
#remote_address = ('192.168.0.105', 8080)
#seconds_duration = 100
#milliseconds_rate = 250
server_socket.bind(local_address)

#run_tests()
#echo_server()
#process_messages(server_socket)


app = QtWidgets.QApplication(sys.argv)
w = MainWindow()
recv_thread = threading.Thread(target=process_messages, args = (server_socket, w))
recv_thread.daemon = True
# TODO KJC remove this print and all others maybe
print("Starting the thread")
recv_thread.start()
print("Just started the thread in the main thread")
app.exec_()







