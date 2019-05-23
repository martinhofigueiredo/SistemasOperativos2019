/*
 * usocat, i.e. micro cat, or may be it should be nano cat
 *        or unix sockets cat
 *
 * This program was inpired in the socat utility.
 * However, it does only one thing:
 *   it establishes a unidirectional "pipe" between
 *    a named Unix socket and standard input/output
 *
 * Invocation:
 *   usocat <src> <dest>
 * where:
 *   <src> is the input end of the pipe
 *   <dest> is the output end of the pipe
 * The standard input/output is denoted by "-", whereas
 *  the named Unix socket is denoted by its pathname
 *
 * To write this I used the man pages and the examples by beej:
 *   "http://beej.us/guide/bgipc/output/html/multipage/unixsock.html"
 */

#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

#include <signal.h>

#include <termios.h>

#define BUF_LEN		512
int s;	// JMC: came to GLOBAL 'cause of sigkill_handler!

struct termios *oldparams = NULL; // for resetting cannonical mode

/* Struct used to represent the pipe */
struct pipe {
    char* src;		/* argv[1] */
    char* dest;		/* argv[2] */
    int infd;		/* source/input file descriptor */
    int outfd;		/* destination/output file descriptor */
};

/* For arguments processing and validation, and debugging */
void process_args(int argc, char *argv[], struct pipe *chan);
void print_usage(char *name);
void display_pipe(struct pipe *chan);
int check_unix_socket(const char *path);

/* Disable stdin buffering */
int dis_stdin_buf();

/* Two main I/O functions */
void receive_data(struct pipe *chan);
void send_data(struct pipe *chan);

/* For output both to the stdout and the UD socket */
void print(int fd, char *buf, int n);

/* The function that prevents the UD from remaining connected */

void close_sock(int s);	// and exit JMC

/* Signal handling */
typedef void (*sighandler_t)(int);
sighandler_t signal(int signo, sighandler_t handler);
void sigkill_handler(int signo);


int main(int argc, char *argv[]) {

    struct pipe chan = { .infd = -1, .outfd = -1};

    process_args(argc, argv, &chan);

    display_pipe(&chan);

    if( signal(SIGINT, sigkill_handler) == SIG_ERR ) {
		perror("Installing signal handler");
		return 0;
    }

    if( chan.infd == STDIN_FILENO ) {
		if( argc == 3 )
			if( dis_stdin_buf() )
				return 1;
		send_data(&chan);
		if( oldparams != NULL ) // suggested by JMC
			tcsetattr(STDIN_FILENO,TCSANOW,oldparams);
    } else if (chan.outfd == STDOUT_FILENO ) {
		receive_data(&chan);
    } else {
		// This should not happen, as process_args() should catch it
		printf("Unexpected configuration: exiting\n");
		return 1;
    }
    return 0;
}

int dis_stdin_buf() {
	struct termios params;

	if((oldparams = malloc(sizeof(struct termios))) == NULL ) {
		perror("dis_stdin_buf::malloc()");
		return 1;
	}

	printf("Disabling stdin buffering ...\n");

	if( tcgetattr(STDIN_FILENO, &params) == -1 ) {
		perror("set_raw_mode::tcgetattr()");
		return 1;
	}

    *oldparams = params; // suggested by JMC
    // set non-cannonical mode
    params.c_lflag &= ~ICANON;
    // read() should return as soon as user types 1 char
    params.c_cc[VMIN] = 1;
    params.c_cc[VTIME] = 0;

    if( tcsetattr(STDIN_FILENO,TCSANOW,&params) == -1 ) {
		perror("set_raw_mode::tcsetattr()");
		return 1;
	}
	return 0;

}

void close_sock(int s) {	// and exit JMC

    if (s != STDOUT_FILENO) {
		// JMC: Clean close of swrite using select()!...
		fd_set rfds;
		struct timeval tv;
		int retval;
		if (shutdown(s, SHUT_WR) == -1) {
		    perror("shutdown swrite WR");
		}
		FD_ZERO(&rfds);
		FD_SET(s, &rfds);	/* Watch swrite to see when it has input. */
		tv.tv_sec = 0;
		tv.tv_usec = 5000;	/* Wait up to five miliseconds. */

		if ((retval = select(1+s, &rfds, NULL, NULL, &tv)) == -1) {	//tv's value can be modified!
		    perror("select()");
		}
		else if (retval) {
			char buf[BUF_LEN];
		    int r;

		    printf("Data is available now.\n");
		    printf("FD_ISSET(s, &rfds): %d\n", FD_ISSET(s, &rfds));

		    if ((r = read(s, buf, BUF_LEN)) >= 0) {
				printf("\nNo of chars read from write sock: %d!\n", r);
				if (shutdown(s, SHUT_RD) == -1) {
				    perror("shutdown swrite RD");
				}
		    }
		    else {
				perror("read in finish");
		    }
		}
		else {
		    printf("No data within five miliseconds.\n");
		}
	}
} // close_sock () JMC

/* To make sure that the socket is closed in an orderly way,
 *  even when the user types Ctrl-C.
 *
 */
void sigkill_handler(int signo) {
    close_sock(s);
    if( oldparams != NULL ) // suggested by JMC
 		tcsetattr(STDIN_FILENO,TCSANOW,oldparams);
 	exit(1);
}


void display_pipe(struct pipe *chan) {
    printf("src = %s : infd = %d\ndest = %s : outfd = %d \n",
	   chan->src, chan->infd, chan->dest, chan->outfd);
}

void print_usage(char *name) {
    printf("%s [-b] <src> <dest>\nwhere:\n"
		   "\t[-b] means buffered stdin (<src> must be \"-\")\n"
		   "\t<src> is the input end of the pipe\n"
	       "\t<dest> is the destination end of the pipe\n", name);
}

int check_unix_socket(const char *path) {
    struct stat sstat;
    char str[64];	// For strerror_r: should be more than enough

    if( stat(path, &sstat) == -1 ) {	// Failed to stat()
		if( strerror_r(errno, str, 256 ) == 0 ) {
		    printf("Failed to stat %s: %s\n", path, str);
		} else { /* Lost errno on strerror_r() */
		    printf("Failed to stat %s\n", path);
		}
		return 0;
    }
    // Stat succeeded: check if this is a Unix socket
    if( !S_ISSOCK(sstat.st_mode) ) {
		printf("%s: is not a named Unix domain socket\n", path);
		return 0;
    } else
		return 1;
}

void process_args(int argc, char *argv[], struct pipe *chan) {
    char *src;
    char *dest;

    if( argc == 3 ) {
		src = argv[1];
		dest = argv[2];
	} else if (argc == 4 ) {
		if( (strlen(argv[1]) != 2) || (strncmp(argv[1], "-b", 2) != 0) ) {
			printf("When using 3 arguments, first one must be \"-b\"\n");
			print_usage(argv[0]);
			exit(1);
		} else if ( (strncmp(argv[2], "-", 1) != 0) || (strlen(argv[2]) != 1)) {
			printf("Buffered input should be used only with the stdin\n");
			print_usage(argv[0]);
			exit(1);
		}
		src = argv[2];
		dest = argv[3];
    } else {
		printf("Wrong number of args: is %d should be either 2 or 3\n", argc-1);
		print_usage(argv[0]);
		exit(1);
    }

    if(strncmp(src,"-", strlen("-")) == 0 ) {
		if( strlen(src) != 1 ) {
		    printf("First argument is not a valid pathname\n");
		    print_usage(argv[0]);
		    exit(1);
		} else if ( check_unix_socket(dest) == 0 ) {
	 	    print_usage(argv[0]);
		    exit(1);
		} else
		    chan->infd = STDIN_FILENO;
    } else if(strncmp(dest,"-", strlen("-")) == 0 ) {
		if( strlen(dest) != 1 ) {
		    printf("Second argument is not a valid pathname\n");
		    print_usage(argv[0]);
		    exit(1);
		} else if ( check_unix_socket(src) == 0 ) {
		    print_usage(argv[0]);
		    exit(1);
		} else
		    chan->outfd = STDOUT_FILENO;
    } else {
		printf("One and only one of the arguments must be \"-\"\n");
		print_usage(argv[0]);
		exit(1);
    }
    chan->src = src;
    chan->dest = dest;
}

/* Receive data from Unix domain socket*/
void receive_data(struct pipe *chan) {
    struct sockaddr_un so_addr;
    char buf[BUF_LEN];
    int r;

    if( (s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ) {
		perror("socket");
		exit(1);
    }

    printf("Trying to connect to Unix domain socket ...\n");

    so_addr.sun_family = AF_UNIX;
    strncpy(so_addr.sun_path, chan->src,sizeof(so_addr.sun_path));
    if( connect(s, (struct sockaddr *)&so_addr, sizeof(so_addr)) == - 1 ) {
		perror("connect");
		exit(1);
    }

    printf("Connected. Receiving data ... \n");

    r = 1; /* To ensure that we execute the loop at least once */

    while( r != 0 ) {
		if( (r = recv(s, buf, BUF_LEN, 0)) == -1 ) {
		    perror("recv");
		    exit(1);
		}
		print(STDOUT_FILENO, buf,r);
    }

    printf("Done receiving data.\n");

    close(s);

}

void print(int s, char *buf, int n) {
    int a, w;

    for( a = 0; a < n; a += w ) {
		if((w = write(s, buf+a, n-a)) == -1) {
		    perror("write to stdout");
		    exit(1);
		}
    }
}

/* Send data to Unix domain socket */
void send_data(struct pipe *chan) {
    struct sockaddr_un so_addr;
    char buf[BUF_LEN];
    int r;

    printf("Starting send loop\n");

    if( (s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ) {
		perror("socket");
		exit(1);
    }

    printf("Trying to connect to Unix domain socket ...\n");

    so_addr.sun_family = AF_UNIX;
    strncpy(so_addr.sun_path, chan->dest, sizeof(so_addr.sun_path));
    if( connect(s, (struct sockaddr *)&so_addr, sizeof(so_addr)) == - 1 ) {
		perror("connect");
		exit(1);
    }

    printf("Connected. Sending data ... \n");

    r = 1; /* To ensure that we execute the loop at least once */
    while( r > 0 ) {	// JMC
		// printf("Reading from stdin ...\n");
		if( (r = read(STDIN_FILENO, buf, 1)) == -1) {
		    perror("read from stdin");
		} else if ( r == 0 ) {
		    printf("No input from stdin \n");
		} else {
		    printf("Writing to socket ...\n");
		    print(s, buf, r);
		}
    }

    printf("Done sending data.\n");

    close_sock(s);	// JMC
}

/* Borrowed from R. Stevens Unix Networking Programing, Vol. 1,
 *  pg. 120, Fig. 5.6 */

sighandler_t signal(int signo, sighandler_t handler) {
    struct sigaction act, oact;

    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if( sigaction(signo, &act, &oact) == -1 ) {
		perror("sigaction");
		return SIG_ERR;
    }
    return(oact.sa_handler);
}
