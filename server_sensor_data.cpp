#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#include <stdio.h>
#include <string.h>

#include <ctype.h>
#include <stdlib.h>

#include <chrono>
#include <thread>
#include <math.h>
#include <string>
#include <assert.h>
#include <asm/param.h>
#include <atomic>
#include <inttypes.h>
#include <iostream>
#include <tuple>

using clk = std::chrono::steady_clock;


/* All the functionality outside of some standard libraries is in this file. These
 * classes provide namespaces, really; all of the functionality is static. Putting
 * headers in the cpp file just to keep things easy. */

/* Simple class just to return a value */
class KJCSensor
{
public:
  static std::pair<int32_t, int32_t> SensorValue(double time);
};

class KJCSensorServer
{
public:
  int Main();

private:
  /* Function running on separate thread to receive and parse commands
   * from network. */
  void CommandParsingThread(int socket, struct sockaddr *peer_address,
                                   socklen_t peer_len,
                                   std::atomic<bool> &received_stop,
                                   std::binary_semaphore &stop_signal,
                                   std::binary_semaphore &wakeup_io_thread);

  /* Network startup */
  void SetupSocket(int *socket_listen, const char *name,
                          const char *service);
  void Cleanup(int socket);

  /* Provide more accurate sleep */
  void SleepSpecial(const clk::time_point &end_timepoint,
                           std::binary_semaphore &stop_signal);

  /***** Functions to parse commands from network ******/
  bool ParseStopCommand(char *read, size_t bytes_received);
  bool ParseIdCommand(char *read, size_t bytes_received);
  bool ParseStartCommand(
      char *read, size_t bytes_received, std::chrono::seconds &duration_seconds,
      std::chrono::microseconds &duration_microseconds,
      std::chrono::milliseconds &rate_milliseconds,
      std::chrono::microseconds &rate_microseconds);

  /**** Network sends ****/
  void SendSensorValue(int socket, struct sockaddr *address,
                              std::pair<int32_t, int32_t> value, clk::time_point current,
                              clk::time_point start);
  void SendStartedMessage(int socket, struct sockaddr *peer_address,
                                 socklen_t peer_len);
  void SendStoppedMessage(int socket, struct sockaddr *peer_address,
                                 socklen_t peer_len);
  void SendErrorAlreadyStartedMessage(int socket,
                                             struct sockaddr *peer_address,
                                             socklen_t peer_len);
  void SendErrorAlreadyStoppedMessage(int socket,
                                             struct sockaddr *peer_address,
                                             socklen_t peer_len);
  void SendDiscoveryMessage(int socket, struct sockaddr *peer_address,
                                   socklen_t peer_len);
  void SendIdleStatusMessage(int socket, struct sockaddr *peer_address,
                                    socklen_t peer_len);
};


std::pair<int32_t, int32_t> KJCSensor::SensorValue(double time)
{
  /* For a time t since we started the sensor, f(t) = sin(2*pi*sqrt(2)*t)
   * or f(t) = sin(2*pi*0.05*t) depending on the commenting below with 
   * t in seconds */
  double frequency = /* sqrt(2.0) */ 0.05;
  double phase = M_PI / 2;
  double amplitude = 1000;
  double argument_millivolts = 2 * M_PI * frequency * time;
  double argument_milliamps = phase + 2 * M_PI * frequency * time;
  double millivolts = sin(argument_millivolts);
  double milliamps = sin(argument_milliamps);
  return std::make_pair<int32_t, int32_t>(amplitude*millivolts, amplitude*milliamps);
}

void KJCSensorServer::SetupSocket(int *socket_listen, const char *name,
                                  const char *service)
{
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *bind_address;
  getaddrinfo(name, service, &hints, &bind_address);

  /* Create the socket */
  *socket_listen = socket(bind_address->ai_family, bind_address->ai_socktype,
                          bind_address->ai_protocol);
  if (*socket_listen < 0)
  {
    fprintf(stderr, "socket() failed. (%d)\n", errno);
    exit(1);
  }

  printf("Binding socket to local address...\n");
  if (bind(*socket_listen, bind_address->ai_addr, bind_address->ai_addrlen))
  {
    fprintf(stderr, "bind() failed. (%d)\n", errno);
    exit(1);
  }
}

void KJCSensorServer::Cleanup(int socket)
{
  printf("Closing listening socket...\n");
  close(socket);
  printf("Finished.\n");
}

void KJCSensorServer::SleepSpecial(const clk::time_point &end_timepoint,
                                   std::binary_semaphore &stop_signal)
{
  /* We get the linux kernel tick rate from HZ (included in <asm/param.h>), turn it into a duration,
   and multiply by 2 to get a duration that we're pretty sure will be larger than our sleep time */
  static std::chrono::duration<double> tick_interval { 1.0 / double(HZ) };
  static std::chrono::duration<double> safe_tick_interval = 2.0 * tick_interval;


  std::chrono::duration<double> sleep_time = ((end_timepoint
      - safe_tick_interval) - clk::now());
      
  bool acquired = false;
  if (sleep_time > std::chrono::duration<double> { 0 })
  {
    //std::cout << std::string{"Before try_acquire_for() in main thread"} << std::endl;
    acquired = stop_signal.try_acquire_for(sleep_time);
    //std::cout << std::string{"After try_acquire_for() in main thread()"} << std::endl;
    //std::cout << std::string{"Was it acquired? Return value of try_acquire_for() "} << acquired << std::endl;

  }
  /* If unblocked by acquiring semaphore then end sleep immediately */
  if (acquired)
  {
    return;
  }
  /* Otherwise spin lock the rest of the time left */
  while (clk::now() < end_timepoint);
}

constexpr std::size_t constexpr_strlen(const char *s)
{
  return std::char_traits<char>::length(s);
}

/* Stop command looks like following: "TEST;CMD=STOP;" */
bool KJCSensorServer::ParseStopCommand(char *read, size_t bytes_received)
{
  constexpr const char *stop_command = "TEST;CMD=STOP;";
  constexpr size_t stop_command_length = constexpr_strlen(stop_command);
  if (bytes_received != stop_command_length)
  {
    return false;
  }
  for (size_t i = 0; i < stop_command_length; ++i)
  {
    if (read[i] != stop_command[i])
    {
      return false;
    }
  }
  return true;
}

/*
 Discovery message looks like following: "ID;"
 In response a message is sent back like: "ID;MODEL=m;SERIAL=n;"
 */
bool KJCSensorServer::ParseIdCommand(char *read, size_t bytes_received)
{
  constexpr const char *id_command = "ID;";
  constexpr size_t id_command_length = constexpr_strlen(id_command);
  if (bytes_received != id_command_length)
  {
    return false;
  }
  for (size_t i = 0; i < id_command_length; ++i)
  {
    if (read[i] != id_command[i])
    {
      return false;
    }
  }
  return true;
}

/* 
 A start command looks like the following: "TEST;CMD=START;DURATION=s;RATE=ms;"
 We parse this as 5 segments, which are:
 - "TEST;CMD=START;DURATION=" also called the first constant segment
 - the duration field, signified by the s
 - ";RATE=" also called the second constant segment
 - the rate field, signified by the ms
 - ";" also called the third constant segment

 If the function returns false, then the character string in read does not match this format.
 If it returns true, we write the seconds and microseconds of the duration into the 3rd and 4th reference parameters,
 and the milliseconds and microseconds of the rate into the 5th and 6th reference parameters.
 */

/* TODO KJC This is too complicated; would better with regular expression parsing, e.g.
   Also parses floating point durations and rates which is unnecessary for the requirements */
bool KJCSensorServer::ParseStartCommand(
    char *read, size_t bytes_received, std::chrono::seconds &duration_seconds,
    std::chrono::microseconds &duration_microseconds,
    std::chrono::milliseconds &rate_milliseconds,
    std::chrono::microseconds &rate_microseconds)
{

  constexpr const char *first_constant_segment = "TEST;CMD=START;DURATION=";
  constexpr const char *second_constant_segment = ";RATE=";
  constexpr const char *separator = ";";
  constexpr const char *decimal_point = ".";
  /* Assigning to a constexpr variable guarantees evaluation at compile time of a constexpr function */
  /* Compute sizes minus the null termination byte */
  constexpr size_t first_constant_segment_size = constexpr_strlen(
      first_constant_segment);
  constexpr size_t second_constant_segment_size = constexpr_strlen(
      second_constant_segment);
  constexpr size_t third_constant_segment_size = constexpr_strlen(separator);

  if (bytes_received < first_constant_segment_size)
  {
    return false;
  }

  /* We know we have a sufficient number of bytes for the first constant section, so we check if they're right */
  size_t current_index;
  for (current_index = 0; current_index < first_constant_segment_size;
      ++current_index)
  {
    if (first_constant_segment[current_index] != read[current_index])
    {
      return false;
    }
  }
  /* After the send of the first segment */
  /* Check that our indices are correct */
  assert(current_index == first_constant_segment_size);
  assert(
      read[current_index - 1]
          == first_constant_segment[first_constant_segment_size - 1]);

  /* Parsing the duration field now. We assume it could contain a decimal point */
  size_t last_index_duration_inclusive;
  size_t first_index_duration_inclusive = current_index;
  bool after_decimal_point = false;

  while (1)
  {
    if (current_index >= bytes_received)
    {
      /* If parsing hasn't finished at the end of recv buffer, we have an incomplete message */
      return false;
    }
    else if (read[current_index] == separator[0])
    {
      /* Check if we're at a separator character indicating the end of the duration field */
      if (current_index == first_constant_segment_size
          || ((current_index == first_constant_segment_size + 1)
              && after_decimal_point))
      {
        /* Duration field was empty, or was just a decimal point */
        return false;
      }
      else
      {
        /* Successfully parsed duration field */
        last_index_duration_inclusive = current_index - 1;
        break;
      }
    }
    else if (read[current_index] == decimal_point[0])
    {
      if (after_decimal_point)
      {
        /* Second decimal point - error */
        return false;
      }
      after_decimal_point = true;
    }
    else if (!isdigit(read[current_index]))
    {
      /* Not a digit or a separator or a decimal point - parsing error */
      return false;
    }
    current_index++;
  }
  size_t length_duration_string = last_index_duration_inclusive
      - first_index_duration_inclusive + 1;
  /* current_index is just past the end of the second section */
  assert(read[current_index] == second_constant_segment[0]);
  assert(
      current_index == (first_constant_segment_size + length_duration_string));
  
  char *duration_string = (char*) malloc(length_duration_string + 1); /* Add spot for a null character at the end */
  if (duration_string == nullptr)
  {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  memcpy(duration_string, read + first_constant_segment_size,
         length_duration_string);
  duration_string[length_duration_string] = '\0';
  if (after_decimal_point)
  {
    double seconds = atof(duration_string);
    duration_seconds = std::chrono::seconds { uint64_t(floor(seconds)) };
    duration_microseconds = std::chrono::microseconds { uint64_t(
        floor((seconds - floor(seconds)) * 1e6)) };
  }
  else
  {
    int64_t seconds = atoi(duration_string);
    duration_seconds = std::chrono::seconds { seconds };
    duration_microseconds = std::chrono::microseconds { 0 };
  }
  free(duration_string);

  /* Now we parse the second fixed part of the record */

  if (bytes_received - (current_index + 1) < second_constant_segment_size)
  {
    return false;
  }
  /* We know we have a sufficient number of bytes for the second constant section, so we check if they're right */
  size_t beginning_second_constant_segment = current_index;
  size_t i;
  for (current_index = beginning_second_constant_segment, i = 0;
      current_index
          < beginning_second_constant_segment + second_constant_segment_size;
      ++current_index, ++i)
  {
    if (second_constant_segment[i] != read[current_index])
    {
      return false;
    }
  }
  /* current_index situated one index past the end of the third segment */
  assert(
      current_index
          == (first_constant_segment_size + length_duration_string
              + second_constant_segment_size));
  assert(
      read[current_index - 1]
          == second_constant_segment[second_constant_segment_size - 1]);

  /* Now we check the rate field. We're assuming that it could contain a decimal point, 
     in which case we want the milliseconds and microseconds */
  size_t first_index_rate_inclusive = current_index;
  size_t last_index_rate_inclusive;
  after_decimal_point = false; /* Resetting after_decimal_point variable from result of duration field parsing */
  while (1)
  {
    if (current_index >= bytes_received)
    {
      /* If parsing hasn't finished at the end of recv buffer, we have an incomplete message */
      return false;
    }
    else if (read[current_index] == separator[0])
    {
      /* Check if we're at a separator character indicating the end of the rate field */
      if (current_index == second_constant_segment_size
          || ((current_index == second_constant_segment_size + 1)
              && after_decimal_point))
      {
        /* rate field was empty, or was just a decimal point */
        return false;
      }
      else
      {
        /* Successfully parsed rate field */
        last_index_rate_inclusive = current_index - 1;
        break;
      }
    }
    else if (read[current_index] == decimal_point[0])
    {
      if (after_decimal_point)
      {
        /* Second decimal point - error */
        return false;
      }
      after_decimal_point = true;
    }
    else if (!isdigit(read[current_index]))
    {
      /* Not a digit or a separator or a decimal point - parsing error */
      return false;
    }
    current_index++;
  }
  /* current_index situated at the separator character at the end of the rate field */
  size_t length_rate_string = (last_index_rate_inclusive
      - first_index_rate_inclusive) + 1;
  /* current_index is just past the end of the fourth section */
  assert(read[current_index] == separator[0]);
  assert(
      current_index
          == (first_constant_segment_size + length_duration_string
              + second_constant_segment_size + length_rate_string));

  char *rate_string = (char*) malloc(length_rate_string + 1); /* Add spot for a null character at the end */
  if (rate_string == nullptr)
  {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  memcpy(
      rate_string,
      read + first_constant_segment_size + length_duration_string
          + second_constant_segment_size,
      length_rate_string);
      
  rate_string[length_rate_string] = '\0';

  if (after_decimal_point)
  {
    double milliseconds = atof(rate_string);
    rate_milliseconds = std::chrono::milliseconds { uint64_t(
        floor(milliseconds)) };
    rate_microseconds = std::chrono::microseconds { uint64_t(
        floor((milliseconds - floor(milliseconds)) * 1e3)) };
  }
  else
  {
    int64_t milliseconds = atoi(rate_string);
    rate_milliseconds = std::chrono::milliseconds { milliseconds };
    rate_microseconds = std::chrono::microseconds { 0 };
  }

  free(rate_string);

  /* Now check that there aren't junk characters at the end that would ruin an otherwise correct message */
  size_t length_entire_message = current_index + 1;
  if (!(length_entire_message == bytes_received))
  {
    return false;
  }

  return true;

}

/* Sensor value messages are formatted like: "STATUS;TIME=ms;MV=mv;MA=ma;" */
/* We are choosing to send time as a function of beginning of measurement */
void KJCSensorServer::SendSensorValue(int socket, struct sockaddr *address,
                                      std::pair<int32_t, int32_t> value, clk::time_point current,
                                      clk::time_point start)
{
  constexpr const char *first_constant_segment = "STATUS;TIME=";
  constexpr const char *second_constant_segment = ";MV=";
  constexpr const char *third_constant_segment = ";MA=";
  constexpr const char *fourth_constant_segment = ";";
  constexpr int first_constant_segment_size = constexpr_strlen(
      first_constant_segment);
  constexpr int second_constant_segment_size = constexpr_strlen(
      second_constant_segment);
  constexpr int third_constant_segment_size = constexpr_strlen(
      third_constant_segment);
  constexpr int fourth_constant_segment_size = constexpr_strlen(
      fourth_constant_segment);
  uint64_t millis = (std::chrono::time_point_cast < std::chrono::milliseconds
      > (current) - std::chrono::time_point_cast < std::chrono::milliseconds
      > (start)).count();
  constexpr int sensor_value_buffer_size = 100;
  char sensor_value_millivolts[sensor_value_buffer_size];
  char sensor_value_milliamps[sensor_value_buffer_size];

  int32_t characters_written_millivolts = snprintf(sensor_value_millivolts,
                                          sensor_value_buffer_size, "%d",
                                          value.first);
  int32_t characters_written_milliamps = snprintf(sensor_value_milliamps,
                                          sensor_value_buffer_size, "%d",
                                          value.second);
  if (characters_written_millivolts < 1
      || characters_written_millivolts >= sensor_value_buffer_size)
  {
    fprintf(stderr, "snprintf() failed on millivolts.\n");
  }
  if (characters_written_milliamps < 1
      || characters_written_milliamps >= sensor_value_buffer_size)
  {
    fprintf(stderr, "snprintf() failed on milliamps.\n");
  }
  constexpr int milliseconds_time_buffer_size = 100;
  char milliseconds_time[milliseconds_time_buffer_size];
  /* Use cross platform macro PRIu64 for uint64_t format specifier in snprintf */
  int characters_written_time = snprintf(milliseconds_time,
                                         milliseconds_time_buffer_size,
                                         "%" PRIu64, millis);
  if (characters_written_time < 1
      || characters_written_time >= sensor_value_buffer_size)
  {
    fprintf(stderr, "snprintf() failed on parse of millisecond field.\n");
  }

  constexpr uint64_t sensor_value_message_buffer_size = 1024;
  char sensor_value_message[sensor_value_message_buffer_size];

  size_t total_message_length = first_constant_segment_size
      + characters_written_time + second_constant_segment_size
      + characters_written_millivolts + third_constant_segment_size
      + characters_written_milliamps + fourth_constant_segment_size
      + 1 /* For null terminator if we printf */;
  /* Check message is not too long */
  if (total_message_length > sensor_value_message_buffer_size)
  {
    fprintf(stderr, "Can send sensor value message, too long.\n");
    exit(1);
  }
  size_t current_index = 0;
  /* "STATUS;TIME=" segment */
  memcpy(&sensor_value_message[current_index], first_constant_segment,
         first_constant_segment_size);
  current_index += first_constant_segment_size;
  /* ms field */
  // TODO KJC could snprintf directly into buffer
  memcpy(&sensor_value_message[current_index], milliseconds_time,
         characters_written_time);
  current_index += characters_written_time;
  /* ";MV=" segment */
  memcpy(&sensor_value_message[current_index], second_constant_segment,
         second_constant_segment_size);
  current_index += second_constant_segment_size;
  /* mv field */
  // TODO KJC could snprintf directly into buffer
  memcpy(&sensor_value_message[current_index], sensor_value_millivolts,
         characters_written_millivolts);
  current_index += characters_written_millivolts;
  /* ";MA=" */
  memcpy(&sensor_value_message[current_index], third_constant_segment,
         third_constant_segment_size);
  current_index += third_constant_segment_size;
  /* ma field */
  // TODO KJC could snprintf directly into buffer
  memcpy(&sensor_value_message[current_index], sensor_value_milliamps,
         characters_written_milliamps);
  current_index += characters_written_milliamps;
  /* ";" segment */
  memcpy(&sensor_value_message[current_index], fourth_constant_segment,
         fourth_constant_segment_size);
  current_index += fourth_constant_segment_size;
  /* We set the index after this last one to zero, which isn't included in the
     UDP message we send out but is useful if we add a debug printf */
  sensor_value_message[current_index + 1] = '\0';
  /* This doesn't include the null terminator character */
  int sensor_value_message_size = current_index /* + 1*/;

  /* TODO KJC pass in the size as a parameter rather than sizeof sockaddr_storage 
     in case a different sockaddr type is used in the future */
  // TODO KJC handle errors on the socket
  ssize_t bytes_sent = sendto(socket, sensor_value_message, sensor_value_message_size, 0, address,
         sizeof(sockaddr_storage));
  if(bytes_sent < 0){
    fprintf(stderr, "Error on sendto(). Errno (%d)\n", errno);
  }
}

void KJCSensorServer::SendStartedMessage(int socket,
                                         struct sockaddr *peer_address,
                                         socklen_t peer_len)
{
  constexpr const char *started_message = "TEST;RESULT=STARTED;";
  constexpr size_t started_message_size = constexpr_strlen(started_message);
  // TODO KJC handle errors on the socket
  ssize_t bytes_sent = sendto(socket, started_message, started_message_size, 0, peer_address,
         peer_len);
  if(bytes_sent < 0){
    fprintf(stderr, "Error on sendto(). Errno (%d)\n", errno);
  }
}
void KJCSensorServer::SendStoppedMessage(int socket,
                                         struct sockaddr *peer_address,
                                         socklen_t peer_len)
{
  constexpr const char *stopped_message = "TEST;RESULT=STOPPED;";
  constexpr size_t stopped_message_size = constexpr_strlen(stopped_message);
  // TODO KJC handle errors on the socket
  ssize_t bytes_sent = sendto(socket, stopped_message, stopped_message_size, 0, peer_address,
         peer_len);
  if(bytes_sent < 0){
    fprintf(stderr, "Error on sendto(). Errno (%d)\n", errno);
  }
}
void KJCSensorServer::SendErrorAlreadyStartedMessage(
    int socket, struct sockaddr *peer_address, socklen_t peer_len)
{
  constexpr const char *error_already_started_message =
      "TEST;RESULT=error;MSG=already_started;";
  constexpr size_t error_already_started_message_size = constexpr_strlen(
      error_already_started_message);
  // TODO KJC handle errors on the socket
  ssize_t bytes_sent = sendto(socket, error_already_started_message,
         error_already_started_message_size, 0, peer_address, peer_len);
  if(bytes_sent < 0){
    fprintf(stderr, "Error on sendto(). Errno (%d)\n", errno);
  }
}
void KJCSensorServer::SendErrorAlreadyStoppedMessage(
    int socket, struct sockaddr *peer_address, socklen_t peer_len)
{
  constexpr const char *error_already_stopped_message =
      "TEST;RESULT=error;MSG=already_stopped;";
  constexpr size_t error_already_stopped_message_size = constexpr_strlen(
      error_already_stopped_message);
  // TODO KJC handle errors on the socket
  ssize_t bytes_sent = sendto(socket, error_already_stopped_message,
         error_already_stopped_message_size, 0, peer_address, peer_len);
  if(bytes_sent < 0){
    fprintf(stderr, "Error on sendto(). Errno (%d)\n", errno);
  }
}
void KJCSensorServer::SendDiscoveryMessage(int socket,
                                           struct sockaddr *peer_address,
                                           socklen_t peer_len)
{
  constexpr const char *discovery_response = "ID;MODEL=1531;SERIAL=4643;";
  constexpr size_t discovery_response_length = constexpr_strlen(
      discovery_response);
  // TODO KJC handle errors on the socket
  ssize_t bytes_sent = sendto(socket, discovery_response, discovery_response_length, 0, peer_address,
         peer_len);
  if(bytes_sent < 0){
    fprintf(stderr, "Error on sendto(). Errno (%d)\n", errno);
  }
}
void KJCSensorServer::SendIdleStatusMessage(int socket,
                                            struct sockaddr *peer_address,
                                            socklen_t peer_len)
{
  constexpr const char *idle_status_message = "STATUS;STATE=IDLE;";
  constexpr size_t idle_status_message_size = constexpr_strlen(
      idle_status_message);
  // TODO KJC handle errors on the socket 
  ssize_t bytes_sent = sendto(socket, idle_status_message, idle_status_message_size, 0, peer_address,
         peer_len);
  if(bytes_sent < 0){
    fprintf(stderr, "Error on sendto(). Errno (%d)\n", errno);
  }
}


/* Listen for commands over the network, parse them, and dispatch */
void KJCSensorServer::CommandParsingThread(
    int socket, struct sockaddr *peer_address, socklen_t peer_len,
    std::atomic<bool> &received_stop, std::binary_semaphore &stop_signal,
    std::binary_semaphore &wakeup_io_thread)
{


  /* The std::chrono variables here are just placeholders because we ignore start
     commands in the thread this function runs in, at least in this version */
  std::chrono::seconds duration_seconds;
  std::chrono::milliseconds rate_milliseconds;
  std::chrono::microseconds duration_microseconds, rate_microseconds;
  /* TODO KJC consider this and other buffers in functions to be in static memory not to pollute stack */
  while (1)
  {
    wakeup_io_thread.acquire();
    char read[1024];
    while (!received_stop)
    {
      printf("Starting the recv loop in the IO thread again\n");
      // TODO KJC check for errors from the recvfrom
      ssize_t bytes_received = recvfrom(socket, read, 1024, 0, peer_address,
                                        &peer_len);
      if(bytes_received < 0){
        fprintf(stderr, "Error on recvfrom(). Errno (%d)\n", errno);
      }
      if (ParseStartCommand(read, bytes_received, duration_seconds,
                            duration_microseconds, rate_milliseconds,
                            rate_microseconds))
      {
        printf("Got a hit on a start command: %.*s\n", (int) bytes_received,
               read);
        /* Send already started message back */
        SendErrorAlreadyStartedMessage(socket, peer_address, peer_len);
      }
      else if (ParseStopCommand(read, bytes_received))
      {
        printf("Got a hit on the stop command: %.*s\n", (int) bytes_received,
               read);
        /* Send already stopped message back. */
        received_stop = true;
        printf("Releasing the semaphore io thread\n");
        stop_signal.release();
        printf("Released the semaphore in the IO thread\n");
        std::this_thread::sleep_for(std::chrono::seconds(0));
        /* The other thread handles the cleanup messages on this path */
      }
      else if (ParseIdCommand(read, bytes_received))
      {
        printf("Got a hit on the id command: %.*s\n", (int) bytes_received,
               read);
        /* Send identification message back */
        SendDiscoveryMessage(socket, peer_address, peer_len);
      }
      else
      {
        // TODO debug out the printf
        printf("Got something that didn't recognize: %.*s\n",
               (int) bytes_received, read);
        /* Don't recognize this message so just ignore it */
      }
    }
  }
}

int KJCSensorServer::Main()
{
  clk::time_point start_timepoint;
  clk::time_point end_timepoint;
  clk::time_point current_timepoint;
  clk::duration rate;

  std::binary_semaphore stop_signal { 0 };
  std::binary_semaphore wakeup_io_thread { 0 };

  /* Create socket */
  int socket_listen;

  SetupSocket(&socket_listen, nullptr, "8080");
  /* Create structure for peer address storage */
  struct sockaddr_storage peer_address;
  socklen_t peer_len = sizeof(sockaddr_storage);
  char read[1024];

  bool send_data { false };

  std::atomic<bool> received_stop { false };
  auto thread1 = std::thread([=, this, &socket_listen, &peer_address, &peer_len, &received_stop, &stop_signal, &wakeup_io_thread]
                              { CommandParsingThread(socket_listen,
                                                      (struct sockaddr*) &peer_address,
                                                      peer_len,
                                                      std::ref(received_stop),
                                                      std::ref(stop_signal),
                                                      std::ref(wakeup_io_thread));});





  std::chrono::seconds duration_seconds;
  std::chrono::milliseconds rate_milliseconds;
  std::chrono::microseconds duration_microseconds, rate_microseconds;

  while (1)
  {
    /* Possibly re-entering from a start cmd after a stop cmd or a survey end */
    send_data = false;
    stop_signal.try_acquire();
    received_stop = false;
    while (!send_data)
    {
      // TODO KJC remove print
      std::cout << "Restarting the recvfrom " << std::endl;
      ssize_t bytes_received = recvfrom(socket_listen, read, 1024, 0,
                                        (struct sockaddr*) &peer_address,
                                        &peer_len);
      if (send_data = ParseStartCommand(read, bytes_received, duration_seconds,
                                        duration_microseconds,
                                        rate_milliseconds, rate_microseconds))
      {
        printf("Got a hit on a start command: %.*s\n", (int) bytes_received,
               read);
        /* Send a starting message back */
        SendStartedMessage(socket_listen, (struct sockaddr*) &peer_address,
                           peer_len);
        /* Shift reading of messages to the IO thread, wake it up */
        wakeup_io_thread.release();
      }
      else if (ParseStopCommand(read, bytes_received))
      {
        printf("Got a hit on the stop command: %.*s\n", (int) bytes_received,
               read);
        /* Send already stopped message back. */
        SendErrorAlreadyStoppedMessage(socket_listen,
                                       (struct sockaddr*) &peer_address,
                                       peer_len);
      }
      else if (ParseIdCommand(read, bytes_received))
      {
        printf("Got a hit on the id command: %.*s\n", (int) bytes_received,
               read);
        /* Send identification message back */
        SendDiscoveryMessage(socket_listen, (struct sockaddr*) &peer_address,
                             peer_len);
      }
      else
      {
        printf("Got something that didn't recognize: %.*s\n",
               (int) bytes_received, read);
        /* Don't recognize this message so just ignore it */
      }
    }

    start_timepoint = clk::now();
    end_timepoint = start_timepoint
        + (duration_seconds + duration_microseconds);
    current_timepoint = start_timepoint;
    rate = rate_milliseconds + rate_microseconds;

    
    bool stop_iterating = false;

    while (!stop_iterating)
    {
      double time_seconds = (std::chrono::duration<double, std::ratio<1,1>> { current_timepoint
          - start_timepoint }).count();
      printf("timeseconds %f\n", time_seconds);
      std::pair<int32_t, int32_t> value = KJCSensor::SensorValue(time_seconds);
      SendSensorValue(socket_listen, (struct sockaddr*) &peer_address, value,
                      current_timepoint, start_timepoint);

      clk::time_point next_timepoint = current_timepoint + rate;
      SleepSpecial(next_timepoint, stop_signal);
      current_timepoint = next_timepoint;
      
      stop_iterating = received_stop || (clk::now() > end_timepoint);
    }
    if (received_stop)
    {
      SendStoppedMessage(socket_listen, (struct sockaddr*) &peer_address,
                         peer_len);
    }
    SendIdleStatusMessage(socket_listen, (struct sockaddr*) &peer_address,
                          peer_len);
  }

  Cleanup(socket_listen);
  return 0;
}

int main()
{
  KJCSensorServer theServer{};
  return theServer.Main();
}

/* TEST;CMD=START;DURATION=3600;RATE=15000; */
/* TEST;CMD=START;DURATION=3600;RATE=1000; */
/* TEST;CMD=START;DURATION=3600;RATE=5000; */
/* TEST;CMD=START;DURATION=3600;RATE=100; */
