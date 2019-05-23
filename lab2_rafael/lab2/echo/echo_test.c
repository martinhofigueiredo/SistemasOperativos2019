#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char *argv[]){
  if (argc != 2){
    printf("Usage: ./echo_test /dev/echo<device_number>\n");
    return -1;
  }
  char *device = argv[1];
  char *buf = "Lab 2 is finally concluded :D";
  char *reads = malloc(strlen(buf)*sizeof(char));

  int fd = open(device, O_RDWR);
  write(fd, buf, strlen(buf)+1);
  read(fd, reads, strlen(buf)+1);
  close(fd);
  return 0;
}
