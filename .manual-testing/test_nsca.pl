#!/usr/bin/perl

use strict;
use warnings;
use Linux::Prctl qw/set_pdeathsig/;
use POSIX;
use Time::HiRes qw /gettimeofday tv_interval stat/;
use Fcntl;
use Linux::Inotify2;
use Data::Dumper;
use Errno qw(EINTR :POSIX);
use IO::Handle;
use IO::Select;
use Getopt::Std;
use File::Basename;

pipe(PIPE_READ_INOTIFY, PIPE_WRITE_INOTIFY);
PIPE_WRITE_INOTIFY->autoflush(1);
pipe(PIPE_READ_FIFO, PIPE_WRITE_FIFO);
PIPE_WRITE_FIFO->autoflush(1);

my $incr = 0;
my $dir_reports = "/tmp/nagiostest";
my $file_fifo = "/tmp/fifo.test";

my $PROCESS_TYPE = "main";

my $NSCA_THREADS_MAX = 1;
my $THREADS_RUNNING = 0;
my $NSCA_THREADS_RUNNING = 0;
my $THREADS_LIST = {};

my $CHECKS_NEEDED = 0;
my $CHECKS_FOUND = 0;
my $TIMEOUT = 1;
my $MTIME_MIN = 0;
my $MTIME_MAX = 0;
my $p_send_nsca = "send_nsca";

my $FIFO_ENABLED = 1;
my $INOTIFY_ENABLED = 1;
my $nsca_cfg = "/etc/nagios/send_nsca.cfg";
my $tmp_nsca_cfg = "/tmp/tmp.send_nsca.cfg";
my $nsca_port = 5667;

sub do_error()
{
    my ($package, $filename, $line, $function_name) = caller(1);
    my $msg = shift || "-";
    my $outmsg = sprintf("%s [%s] ERROR > [%s:%d] %s() - %s\n", scalar localtime, $PROCESS_TYPE, basename($filename), $line, $function_name, $msg);
    print STDERR $outmsg;
    exit(1);
}

sub do_warn() {
    my ($package, $filename, $line, $function_name) = caller(1);
    my $msg = shift || "-";
    my $outmsg = sprintf("%s [%s] WARN  > [%s:%d] %s()  - %s\n", scalar localtime, $PROCESS_TYPE, basename($filename), $line, $function_name, $msg);
    print $outmsg;
}

sub do_info()
{
    my $msg = shift || "-";
    my $outmsg = sprintf("%s [%s] INFO  > %s\n", scalar localtime, $PROCESS_TYPE, $msg);
    print $outmsg;
}

sub handle_alarm()
{
    #    printf("[main] - Failed - alarm reached after (%d seconds) [files_needed=%d, files_found=%d]\n", $TIMEOUT, $FILES_NEEDED, $FILES_FOUND);
    #    &fullcleanup(1);
    #    exit(1);
    #    print "ALRM()\n";
}

sub handle_child()
{
    while( ( my $child = waitpid( -1, &WNOHANG ) ) > 0 ) {
        $THREADS_RUNNING--;
        if (defined($THREADS_LIST->{"$child"})) {
            my $ptype = ${$THREADS_LIST->{"$child"}}[1];
            my $spend_time = tv_interval ${$THREADS_LIST->{"$child"}}[0], [ gettimeofday ];
            &do_info(sprintf("[$ptype(%d)] Finished in %.3fs", $child, $spend_time));
            if ($ptype eq 'nsca') {
                $NSCA_THREADS_RUNNING--;
            }
            delete $THREADS_LIST->{"$child"};
        }
    }
}


sub gen_report()
{
    my $reports = shift || 1;
    my @out = ();
    for (my $i=0; $i<$reports; $i++) {
        push(@out, "hostname_$incr\tservice_$incr\toutput_$incr");
        $incr++;
    }
    my $strin = join("\x17", @out);
    return $strin;
}

sub nsca()
{
    my @nsca_command = ($p_send_nsca, "-H", "127.0.0.1", "-p", $nsca_port, "-to", "15", "-c", $nsca_cfg);
    my $pid = fork();
    if ($pid > 0) {
        #main
        my $started = [gettimeofday];
        $THREADS_LIST->{"$pid"} = [$started, "nsca"];
        $THREADS_RUNNING++;
        $NSCA_THREADS_RUNNING++;
    } else {
        close(PIPE_WRITE_INOTIFY);
        close(PIPE_READ_INOTIFY);
        close(PIPE_WRITE_FIFO);
        close(PIPE_READ_FIFO);
        set_pdeathsig(9);
        $PROCESS_TYPE = "nsca";
        my $strin = shift || "";
        local *P;
        open(P, "|-", @nsca_command)  or die($!);
        print P $strin ."\n";
        close(P);
        exit(0);
    }
}

sub inotify_thread()
{
    my $pid = fork();
    if ($pid > 0) {
        #main
        my $started = [gettimeofday];
        $THREADS_LIST->{"$pid"} = [$started, "inotify"];
        $THREADS_RUNNING++;
        close(PIPE_WRITE_INOTIFY);
    } else {
        close(PIPE_READ_FIFO);
        close(PIPE_READ_INOTIFY);

        close(PIPE_WRITE_FIFO);
        set_pdeathsig(9);
        $PROCESS_TYPE = "inotify";
        &inotify_loop();
        exit(0);
    }
}

sub fifo_thread()
{
    my $pid = fork();
    if ($pid > 0) {
        #main
        my $started = [gettimeofday];
        $THREADS_LIST->{"$pid"} = [$started, "fifo"];
        $THREADS_RUNNING++;
        close(PIPE_WRITE_FIFO);
    } else {;
        close(PIPE_READ_FIFO);
        close(PIPE_READ_INOTIFY);

        close(PIPE_WRITE_INOTIFY);
        set_pdeathsig(9);
        $PROCESS_TYPE = "fifo";
        &fifo_loop();
        exit(0);
    }
}

sub inotify_loop()
{
    my $files_found_local = 0;
    &fullcleanup();
    my $inotify = Linux::Inotify2->new;
    $inotify->watch("$dir_reports", IN_CLOSE_WRITE);
    my $inactivity = 2;
    my $last_event = time();
    my $we_found_one = 0;
    print PIPE_WRITE_INOTIFY "INOTIFY_INIT\n";
    alarm(1);
    while () {
        my @events = $inotify->read;
        my $now = time();
        unless (@events > 0){
            if ($!{EINTR}) {
                # just interrupt
                if ($now-$last_event > 2 && $we_found_one) {
                    print PIPE_WRITE_INOTIFY "INOTIFY_ERROR timeout:$inactivity\n";
                    last;
                }
                alarm(1);
            } else {
                print PIPE_WRITE_INOTIFY "INOTIFY_ERROR $!\n";
                last;
            }
        }
        foreach my $e (@events) {
            if ($e->{w}{mask} & IN_CLOSE_WRITE) {
                if( $e->{name} =~ /^c.{6}(\.ok)?$/ ) {
                    $we_found_one = 1;
                    $files_found_local++;
                    $last_event = $now;
                }
            }
        }
        print PIPE_WRITE_INOTIFY "INOTIFY $files_found_local\n";
    }
    print PIPE_WRITE_INOTIFY "INOTIFY_FINISH\n";
    alarm(0);
    &fullcleanup(1);
}

sub fifo_loop()
{
    local *FIFO;
    print PIPE_WRITE_FIFO "FIFO_INIT\n";
    if (open(FIFO, "+<$file_fifo")) {
        while (<FIFO>) {
            chomp;
            if ($_ =~ /^\[[0-9]+\]/) {
                print PIPE_WRITE_FIFO "FIFO 1\n";
            }
        }
        close(FIFO);
    } else {
        print PIPE_WRITE_FIFO "FIFO_ERROR $!\n";
    }
}

sub check()
{
    my $found = 0;
    local *D;
    opendir(D, "$dir_reports") or die($!);
    while (my $fn = readdir(D)) {
        next if ($fn eq '.' or $fn eq '..');
        my $fp = $dir_reports ."/$fn";
        next if (!-f $fp);
        my $flen = length($fn);
        next if ($flen != 7 && $flen != 10);
        next if (substr($fn, 0, 1) ne 'c');
        $found++;
    }
    closedir(D);
    return $found;
}

sub fullcleanup() {
    my $timestat = shift || 0;
    local *D;
    opendir(D, "$dir_reports") or die($!);
    while (my $fn = readdir(D)) {
        next if ($fn eq '.' or $fn eq '..');
        my $fp = $dir_reports ."/$fn";
        next if (!-f $fp);
        my $flen = length($fn);
        next if ($flen != 7 && $flen != 10);
        next if (substr($fn, 0, 1) ne 'c');
        if ($timestat) {
            my @stat = stat($fp);
            if (scalar @stat) {
                my $mtime = $stat[9];
                if ($MTIME_MIN == 0) {
                    $MTIME_MIN = $mtime;
                    $MTIME_MAX = $mtime;
                } else {
                    if ($mtime < $MTIME_MIN) {
                        $MTIME_MIN = $mtime;
                    }
                    if ($mtime > $MTIME_MAX) {
                        $MTIME_MAX = $mtime;
                    }
                }
            }
        }
        unlink($fp);
    }
    closedir(D);
    if ($timestat) {
        my $diff = $MTIME_MAX-$MTIME_MIN;
        &do_info(sprintf("[fullcleanup] nsca spent %.3fs to create the files(%d), speed: %.2f/s\n", $diff, $CHECKS_FOUND, $diff > 0 ? $CHECKS_FOUND/2/$diff : 0));
    }
}

sub process_nsca_cfg()
{
    my $original_nsca_cfg = shift;
    my $encryption_method = 1;
    my $password = "";
    local *F;
    open(F, $original_nsca_cfg) or &do_error("Unable to open file($original_nsca_cfg) for reading ($!)");
    while (<F>) {
        chomp;
        if ($_ =~ /^\s*decryption_method\s*=\s*([0-9]+)/) {
            $encryption_method = int($1);
        } elsif ($_ =~ /^\s*password\s*=\s*(.*)\s*$/) {
            $password = $1;
        } elsif ($_ =~ /^\s*server_port\s*=\s*([0-9]+)/) {
            my $tmp_nsca_port = int($1);
            if ($tmp_nsca_port != $nsca_port) {
                &do_info("nsca_port changed: $nsca_port -> $tmp_nsca_port");
                $nsca_port = $tmp_nsca_port;
            }
        }
    }
    close(F);
    open(F, ">$tmp_nsca_cfg") or &do_error("Unable to open $tmp_nsca_cfg for writing ($!)");
    print F "encryption_method=$encryption_method\n";
    print F "password=$password\n";
    close(F);
    $nsca_cfg = $tmp_nsca_cfg;
}

sub do_help()
{
    print "$0 -c <checks per nsca> -t <nsca threads> -f <fifo_path or 0> -i <check_result_path or 0> -n <nsca server config as template> -b <send_nsca binary>\n";
    exit(1);
}

sub main()
{
    $SIG{ALRM} = \&handle_alarm;
    $SIG{CHLD} = \&handle_child;
    my $opts = {
        'f' => "",
        'i' => "",
        'n' => "",
        'b' => "",
        'c' => 1,
        't' => 1
    };
    getopts("t:c:f:i:hn:b:", $opts);

    if (defined($opts->{'h'})) {
        &do_help();
    }
    if ($opts->{'f'} eq "0") {
        &do_info("FIFO disabled.");
        $FIFO_ENABLED = 0;
    } elsif (length($opts->{'f'}) > 0) {
        $file_fifo = $opts->{'f'};
    }

    if (-e $file_fifo) {
        if (!-p $file_fifo) {
            &do_error("Given file($file_fifo) is not a pipe. Exiting.");
        }
    } else {
        &do_info("Creating $file_fifo, 0644");
        mkfifo($file_fifo, 0644) or &do_error("Cannot create fifo: $file_fifo ($!)");
    }

    if ($opts->{'n'} ne "") {
        $nsca_cfg = $opts->{"n"};
    }
    if (!-r $nsca_cfg) {
        &do_error("nsca config doest not exist: $nsca_cfg");
    }
    &process_nsca_cfg($nsca_cfg);

    if ($opts->{'i'} eq "0") {
        &do_info("inotify/check_result_path disabled.");
        $INOTIFY_ENABLED = 0;
    } elsif (length($opts->{'i'}) > 0) {
        $dir_reports = $opts->{'i'};
    }
    if (!-d $dir_reports) {
        &do_error("directory does not exist: $dir_reports");
    }

    if ($opts->{'b'} ne "") {
        $p_send_nsca = $opts->{'b'};
    }

    my $reports = int($opts->{'c'});
    $NSCA_THREADS_MAX = int($opts->{'t'});
    $NSCA_THREADS_MAX = int($NSCA_THREADS_MAX);

    $CHECKS_NEEDED = $NSCA_THREADS_MAX*$reports;
    $CHECKS_FOUND = 0;
    $TIMEOUT = 1;
    set_pdeathsig(9);

    if ($INOTIFY_ENABLED) {
        &inotify_thread();
    }
    if ($FIFO_ENABLED) {
        &fifo_thread();
    }

    if (!$FIFO_ENABLED && !$INOTIFY_ENABLED) {
        &do_error("Either inotify or fifo should be enabled.");
    }

    my $files_found = 0;
    my $fifo_checks = 0;
    my $checks_done = 0;
    my $checks_done_files = 0;
    &do_info("waiting for inotify & fifo");

    &do_info("spawning send_nsca processes: $NSCA_THREADS_MAX");
    for (my $i=0; $i<$NSCA_THREADS_MAX; $i++) {
        &nsca(&gen_report($reports));
    }
    while (1) {
        if ($NSCA_THREADS_RUNNING == 0) {
            last;
        }
        sleep(1);
    }
    &do_info("All send_nsca process finished.");


    my $s = IO::Select->new();
    if ($INOTIFY_ENABLED) {
        $s->add(\*PIPE_READ_INOTIFY);
    }
    if ($FIFO_ENABLED) {
        $s->add(\*PIPE_READ_FIFO);
    }
    my $finish_read = 0;
    my $inactivity_timeout = 3;
    my $last_data = time();
    my $started = $last_data;
    while (1) {
        my @ready = $s->can_read(1);
        my $now = time();
        foreach my $fh (@ready) {
            my $chr = sysread($fh, my $lines, 65536);
            next if ($chr == 0);
            my $update_last_data = 1;
            my @lines = split(/\n/, $lines);
            foreach my $read (@lines) {
                if ($read =~ /^INOTIFY ([0-9]+)$/) {
                    $files_found = int($1);
                    if ($files_found == 0) {
                        $update_last_data = 0;
                    }
                }
                elsif ($read =~ /^INOTIFY_INIT$/) {
                    &do_info("Inotify::watch started.");
                }
                elsif ($read =~ /^FIFO_INIT$/) {
                    &do_info("FIFO listen started.");
                }
                elsif ($read =~ /^FIFO_ERROR (.+)$/) {
                    &do_warn("FIFO_ERROR: $1");
                }
                elsif ($read =~ /^FIFO 1/) {
                    $fifo_checks++;
                }
                elsif ($read =~ /^INOTIFY_ERROR (.+)$/) {
                    &do_warn("INOTIFY_ERROR: $1");
                }
                elsif ($read =~ /^INOTIFY_FINISH$/) {
                    &do_info("Inotify finished.");
                    $s->remove(\*PIPE_READ_INOTIFY);
                }
                $checks_done_files = int($files_found) / 2;
                $checks_done = $fifo_checks + $checks_done_files;
                if ($checks_done == $CHECKS_NEEDED) {
                    $finish_read = 1;
                }
            }
            if ($update_last_data == 1) {
                $last_data = $now;
            }
        }
        if ($finish_read) {
            last;
        }
        if ($now-$started > $inactivity_timeout && $checks_done == 0) {
            &do_warn("inactivity timeout reached: $inactivity_timeout, no initial data.");
            last;
        }
        if ($now-$last_data > $inactivity_timeout) {
            &do_warn("inactivity timeout reached: $inactivity_timeout");
            last;
        }
    }
    if ($finish_read) {
        &do_info("everything is ok ($checks_done). (fifo_checks=$fifo_checks, files_found: $files_found)");
    } else {
        &do_warn("we have failed checks (done: $checks_done, needed: $CHECKS_NEEDED). (fifo_checks=$fifo_checks, files_found: $files_found)");
    }

    while (1) {
        if ($THREADS_RUNNING == 0) {
            last;
        } else {
            foreach my $cpid (keys %{$THREADS_LIST}) {
                if (${$THREADS_LIST->{"$cpid"}}[1] eq 'fifo') {
                    kill 9, $cpid;
                }
                if (${$THREADS_LIST->{"$cpid"}}[1] eq 'inotify') {
                    kill 9, $cpid;
                }
            }
        }
        sleep(1);
    }
    unlink($tmp_nsca_cfg);
}

&main();
