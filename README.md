# nsca_fast
Faster, high-performance NSCA server with worker and threadpool support. Drop-in replacement.

![build](https://github.com/macskas/nsca_fast/actions/workflows/cmake.yml/badge.svg)

### problems with old NSCA server
  - If you have a high traffic NSCA server you will probably end up with a lot of child processes.
  - connection timeouts are not handled and child processes will stuck there forever
  - if you use any encryption method(mcrypt) its gonna block the read_connection
  - since systemd is present on new ubuntu releases, many forks + openlog() syslog will just not work. and without fork mode you wont be able to handle large scale monitoring

### why not just use nsca-ng, etc?
  - who wants to change clients send_nsca binary? who wants to use different protocol?
  - this is just a drop-in replacement for nsca server. You can keep the old configuration if you want. Only a few new option is introduced.
  - I use mostly the orginal nsca/utils.h nsca/utils.c and it can be upgraded in no time if the core nagios will change anything in their code.
  
### Performance change with real world example:
 - HW: CPU E-2136 (cpubenchmark.net: 13K)
 - nsca.cfg settings: encryption_method=3
 - original nsca, anything without systemd: ~850 checks/seconds.  above that it started dropping checks but sometimes it scaled up to ~5k checks/seconds
 - original nsca, ubuntu bionic+systemd (~850 checks/seconds) above that it started dropping checks(timeouts) and could not scale up at all. full restart was needed
 - nsca_fast: 14k checks/seconds

### new options:
```
decryption_mode=1 # default is 1, this is faster. 0 is more secure
nsca_workers=4
nsca_threads_per_worker=8
max_checks_per_connection=5000
max_packet_age_enabled=0 # by default packet age check is disabled in the original nsca server even if you set max_packet_age. I keep it that way, but you can override nagios core behaviour by settings this value=1. I dont think its a good idea if the NTP sync is disabled on the clients.
check_result_path_max_files=0 # if nagios core process is not running it wont process check_result_path files, so it might fill up the disk eventually. To avoid that you can specify a maximum number of unprocessed files. (it uses inotify instead of dir listing. I means its a low io operation even if you use physical disk and not tmpfs)
```
- As you can see it could use fix workers (fork) with the kernel's REUSEPORT support.
- You can use fix size thread pools in workers. So you wont end up with infinite workers and infinite thread pools.
- It supports fifo and check_result_path at the same time if you set both. First it writes the result into fifo and if it fails it saves it in the check_result_path


### dependency
  - mcrypt
  - libevent2
  
  to compile it on ubuntu 18.04:
  ```
  # apt-get install libevent-dev libmcrypt-dev cmake make g++
  # # chdir into directory
  # cmake .
  # make -j5
  ```
  
### cli

```
root@server:~# nsca -h
Usage: nsca [OPTIONS]
nsca.

Mandatory arguments to long options are mandatory for short options too.
  -h                    this screen
  -c [FILE]             configfile
  -d                    verbose output
  -f                    foreground
  -n [MAX_WORKERS]      max workers - between 0 and 100
  -t [THREADS]          max_threads_per_worker - between 0 and 1000

```

### debug mode
```
root@server:~# nsca -d -f -n 1 -t 1
2020-12-18 13:04:29 DEBUG [23697] > [config] debug=0
2020-12-18 13:04:29 DEBUG [23697] > [config] pid_file='/tmp/nsca.pid'
2020-12-18 13:04:29 DEBUG [23697] > [config] server_address='0.0.0.0'
2020-12-18 13:04:29 DEBUG [23697] > [config] server_port=5667
2020-12-18 13:04:29 DEBUG [23697] > [config] nsca_user='nobody'
2020-12-18 13:04:29 DEBUG [23697] > [config] nsca_group='nogroup'
2020-12-18 13:04:29 DEBUG [23697] > [config] command_file='/tmp/testfifo'
2020-12-18 13:04:29 DEBUG [23697] > [config] check_result_path='/tmp/nagiostest'
2020-12-18 13:04:29 DEBUG [23697] > [config] max_packet_age=200
2020-12-18 13:04:29 DEBUG [23697] > [config] max_packet_age_enabled=0
2020-12-18 13:04:29 DEBUG [23697] > [config] decryption_method=3
2020-12-18 13:04:29 DEBUG [23697] > [config] password='passwordtest'
2020-12-18 13:04:29 DEBUG [23697] > [config] nsca_workers='1'
2020-12-18 13:04:29 DEBUG [23697] > [config] nsca_threads_per_worker='1'
2020-12-18 13:04:29 DEBUG [23697] > [config] max_checks_per_connection='5000'
2020-12-18 13:04:29 DEBUG [23697] > [config] decryption_mode/SHARED_CRYPT_INSTANCE=1
2020-12-18 13:04:29 DEBUG [23697] > [void writepid()] pidfile=/tmp/nsca.pid, pid=23697
2020-12-18 13:04:29 DEBUG [23697] > [processManager::loop] started
2020-12-18 13:04:29 DEBUG [23697] > [super_downgrade] Changed groupname=nobody, gid=1000
2020-12-18 13:04:29 DEBUG [23697] > [super_downgrade] Changed username=nogroup, uid=1000
2020-12-18 13:04:29 DEBUG [23697] > [void processManager::work()] started
2020-12-18 13:04:29 DEBUG [23697] > [fifo_client::fifo_client(std::__cxx11::string)]
2020-12-18 13:04:29 DEBUG [23697] > [void network::run()]
2020-12-18 13:04:29 DEBUG [23697] > [void crypt_thread_t::loop()] started.
2020-12-18 13:04:30 INFO  [23697] > [network statistics] 0 conn/s, connections=0, report_success=0, report_failed=0
2020-12-18 13:04:35 INFO  [23697] > [network statistics] 0 conn/s, connections=0, report_success=0, report_failed=0
^C2020-12-18 13:04:36 DEBUG [23697] > [int network::stop()]
2020-12-18 13:04:37 DEBUG [23697] > [void network::run()] finished.
2020-12-18 13:04:37 DEBUG [23697] > [fifo_client::~fifo_client()]
2020-12-18 13:04:37 DEBUG [23697] > [void crypt_thread_t::join()] join.
2020-12-18 13:04:37 DEBUG [23697] > [void crypt_thread_t::loop()] finished.
2020-12-18 13:04:37 DEBUG [23697] > [void crypt_thread_t::join()] joined.
2020-12-18 13:04:37 DEBUG [23697] > [processManager::loop] finished

```
