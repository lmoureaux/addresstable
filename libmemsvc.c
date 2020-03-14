#include <errno.h>
#include <linux/un.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>

#include "libmemsvc.h"

uint32_t THE_REG = 0;
//#define REG32(baseptr, offset) (THE_REG)
#define REG32(baseptr, offset) (*((volatile uint32_t*)(((uint8_t*)baseptr)+(offset))))

struct memsvc_range {
  uint32_t start;
  uint32_t end;
  uint8_t readonly;

  void *map;
  uint32_t maplen;

  void *c2creg_map;
  uint32_t c2creg_offset;
  //
  uint32_t c2c_reg;
  uint32_t c2c_mask;

  struct memsvc_range *next;
};

struct memsvc_handle {
#define MEMSVC_VALID_HANDLE_MAGIC 0x481ffe3c
#define MEMSVC_INVALID_HANDLE_MAGIC 0x481ffe3d
  uint32_t magic;
  char last_error[512];

  int memfd;

  struct memsvc_range *first_range;
};

static inline void set_error(memsvc_handle_t svc, const char *user, int err)
{
  memset(svc->last_error, 0, 512);
  if (user)
    strncpy(svc->last_error, user, 512);
  if (err) {
    char errbuf[512];
    errno = 0;
    strerror_r(err, errbuf, 512);
    errbuf[511] = '\0';

    strncpy(svc->last_error+strlen(svc->last_error), errbuf, 512-strlen(svc->last_error));
    svc->last_error[511] = '\0';
  }
}

static struct memsvc_range *find_range(struct memsvc_handle *svc, uint32_t addr, uint8_t writeable, uint8_t activate)
{
  if (addr % 4)
    return NULL;

  struct memsvc_range *prev = NULL;

  for (struct memsvc_range *cur = svc->first_range; cur != NULL; prev = cur, cur = cur->next) {
    if (cur->start <= addr && addr <= cur->end && (!writeable || !cur->readonly)) {
      // Found it!
      if (svc->first_range != cur) {
        // Relink its child to its parent's tail.
        prev->next = cur->next;
        cur->next = NULL;

        // Move it to the front of the list.
        cur->next = svc->first_range;
        svc->first_range = cur;
      }

      int pagesize = 0;
      if (activate && !cur->map) {
        pagesize = getpagesize();
        cur->maplen = cur->end - cur->start + 1;
        if (cur->maplen % pagesize)
          cur->maplen += pagesize - (cur->maplen % pagesize);

        cur->map = mmap(NULL, cur->maplen, PROT_READ|(cur->readonly ? 0 : PROT_WRITE), MAP_SHARED, svc->memfd, cur->start);
#if 1
        if (cur->map == MAP_FAILED) {
          cur->map = NULL;
          activate = 0;
        }
#endif
      }

      if (activate && !cur->c2creg_map) {
        if (!pagesize) pagesize = getpagesize();
        cur->c2creg_offset = cur->c2c_reg % pagesize;
        cur->c2creg_map = mmap(NULL, pagesize, PROT_READ, MAP_SHARED, svc->memfd, cur->c2c_reg - cur->c2creg_offset);
        if (cur->c2creg_map == MAP_FAILED)
          cur->c2creg_map = NULL;
      }

      return cur;
    }
  }
  return NULL;
}

static void free_ranges(struct memsvc_range *cur)
{
  if (!cur)
    return;

  do {
    if (cur->map)
      munmap(cur->map, cur->maplen);
    if (cur->c2creg_map)
      munmap(cur->c2creg_map, getpagesize());
    struct memsvc_range *next = cur->next;
    free(cur);
    cur = next;
  } while (cur);
}

static void memsvc_invalidate(memsvc_handle_t svc)
{
  if (svc->magic != MEMSVC_VALID_HANDLE_MAGIC && svc->magic != MEMSVC_INVALID_HANDLE_MAGIC)
    return;

  svc->magic = MEMSVC_INVALID_HANDLE_MAGIC;
  if (svc->memfd != -1) {
    close(svc->memfd);
    svc->memfd = -1;
  }
  free_ranges(svc->first_range);
  svc->first_range = NULL;
}

static inline int memsvc_validate(memsvc_handle_t svc, int validity)
{
  if (!svc)
    return -1;
  if (svc->magic != MEMSVC_VALID_HANDLE_MAGIC && svc->magic != MEMSVC_INVALID_HANDLE_MAGIC)
    return -1;

  if (svc->magic == MEMSVC_INVALID_HANDLE_MAGIC && validity) {
    set_error(svc, "Invalid service handle", 0);
    return -1;
  }
  return 0;
}

static int recvfd(int sock, int *fd, void *buf, int buflen)
{
  struct iovec    iov[1];
  iov[0].iov_base = buf;
  iov[0].iov_len  = buflen;

  struct msghdr   msg;
  msg.msg_iov     = iov;
  msg.msg_iovlen  = 1;
  msg.msg_name    = NULL;
  msg.msg_namelen = 0;

  char ctrl[1024];
  msg.msg_control    = ctrl;
  msg.msg_controllen = 1024;

  int rv = recvmsg(sock, &msg, 0);
  if (rv <= 0)
    return rv;

  *fd = -1;

  for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type  == SCM_RIGHTS) {
      int *rcvdfd = (int*)CMSG_DATA(cmsg);
      *fd = *rcvdfd;
      break;
    }
  }

  return rv;
}

int memsvc_open(memsvc_handle_t *handle)
{
  struct memsvc_handle *svc = malloc(sizeof(struct memsvc_handle));
  memset(svc, 0, sizeof(struct memsvc_handle));
  svc->memfd = -1;
  svc->magic = MEMSVC_INVALID_HANDLE_MAGIC;

  *handle = svc;
#if 0
  FILE *f = fopen("/proc/self/exe", "r"); // Why not
  if (!f) {
    set_error(svc, "Could not open file: ", errno);
    memsvc_invalidate(svc);
    return -1;
  }
  svc->memfd = fileno(f);

#else
  int sockfd = socket(PF_UNIX, SOCK_STREAM, 0);
  if (sockfd < 0) {
    set_error(svc, "Socket creation failed: ", errno);
    memsvc_invalidate(svc);
    return -1;
  } 

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(struct sockaddr_un));

  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, UNIX_PATH_MAX, "#wisc_memsvc");
  addr.sun_path[0] = '\0';

  if (connect(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) != 0) {
    set_error(svc, "Unable to connect to memsvc: ", errno);
    memsvc_invalidate(svc);
    close(sockfd);
    return -1;
  }
	
  char nulbuf;
  if (recvfd(sockfd, &(svc->memfd), &nulbuf, 1) != 1) {
    close(sockfd);
    set_error(svc, "Unable to retrieve memory descriptor from memsvc: ", errno);
    memsvc_invalidate(svc);
    return -1;
  }
  close(sockfd);

  if (svc->memfd < 0) {
    set_error(svc, "Unable to retrieve memory descriptor from memsvc", 0);
    memsvc_invalidate(svc);
    return -1;
  }
#endif

  /* Parse Config File & Init ranges
   */
   FILE *cf = fopen("/etc/memsvc.conf", "r");
//  FILE *cf = fopen("memsvc.conf", "r");
  if (!cf) {
    set_error(svc, "Unable to open memsvc configuration file: ", errno);
    memsvc_invalidate(svc);
    return -1;
  }

  char buf[1024];
  int config_error = 0;
  int lineno = 0;
  while (fgets(buf, 1024, cf) != NULL) {
    lineno++;

    if (buf[0] == '#')
      continue;

    struct memsvc_range *range = malloc(sizeof(struct memsvc_range));
    memset(range, 0, sizeof(struct memsvc_range));

    char *parsebuf = buf;
    errno = 0;
    char *endptr = NULL;
    range->start = strtoul(parsebuf, &endptr, 16);
    if (!endptr || *endptr != ' ' || errno) {
      config_error = 1;
      free(range);
      break;
    }

    parsebuf = endptr+1;

    errno = 0;
    endptr = NULL;
    range->end = strtoul(parsebuf, &endptr, 16);
    if (!endptr || *endptr != ' ' || errno) {
      config_error = 2;
      free(range);
      break;
    }

    parsebuf = endptr+1;

    if (*parsebuf == '0')
      range->readonly = 0;
    else if (*parsebuf == '1')
      range->readonly = 1;
    else {
      config_error = 3;
      free(range);
      break;
    }
    parsebuf++;
    if (*parsebuf != ' ') {
      config_error = 3;
      free(range);
      break;
    }

    parsebuf++;

    errno = 0;
    endptr = NULL;
    range->c2c_reg = strtoul(parsebuf, &endptr, 16);
    if (!endptr || *endptr != ' ' || errno) {
      config_error = 4;
      free(range);
      break;
    }

    parsebuf = endptr+1;

    errno = 0;
    endptr = NULL;
    range->c2c_mask = strtoul(parsebuf, &endptr, 16);
    if (!endptr || (*endptr != ' ' && *endptr != '\n' && *endptr != '\0') || errno) {
      config_error = 5;
      free(range);
      break;
    }

    range->next = svc->first_range;
    svc->first_range = range;

    if (range->start >= range->end) {
      char cfgerr[80];
      snprintf(cfgerr, 80, "Config file error on line %d: start[0x%08x]>=end[0x%08x]", lineno, range->start, range->end);
      set_error(svc, cfgerr, 0);
      fclose(cf);
      memsvc_invalidate(svc);
      return -1;
    }
  }
  fclose(cf);

  if (config_error) {
    char cfgerr[64];
    snprintf(cfgerr, 64, "Config file error on line %d, on or after field %d", lineno, config_error);
    set_error(svc, cfgerr, 0);
    memsvc_invalidate(svc);
    return -1;
  }

  svc->magic = MEMSVC_VALID_HANDLE_MAGIC;
  return 0;
}

int memsvc_close(memsvc_handle_t *svc)
{
  if (!svc || !*svc)
    return 0;

  memsvc_invalidate(*svc);
  free(*svc);
  *svc = NULL;
  return 0;
}

const char *memsvc_get_last_error(memsvc_handle_t svc)
{
  return (const char *)(svc->last_error);
}

static int range_check(struct memsvc_handle *svc, struct memsvc_range *range, uint32_t addr, uint32_t words)
{
  if (!range->map) {
    set_error(svc, "Unable to map memory range", 0);
  }

  if (!range->c2creg_map) {
    set_error(svc, "Unable to map AC2C status register memory range", 0);
  }

  if (addr < range->start || range->end < addr+(4*words)) {
    set_error(svc, "Address out of range", 0);
    return -1;
  }

  if (range->c2c_reg) {
    if ((REG32(range->c2creg_map, range->c2creg_offset) & range->c2c_mask) != range->c2c_mask) {
      set_error(svc, "Chip2Chip bridge offline", 0);
      return -1;
    }
    // TODO: Validate 
  }
  return 0;
}

static sigjmp_buf sj_env;

static void sigbushdl (int sig, siginfo_t *siginfo, void *ptr)
{
  /* do something */
  /* printf("SIGBUS handled. jumping to return\n"); */
  siglongjmp (sj_env, 1);
}


int memsvc_read(memsvc_handle_t svc, uint32_t addr, uint32_t words, uint32_t *data)
{
  if (memsvc_validate(svc, 1) < 0)
    return -1;
  struct memsvc_range *range = find_range(svc, addr, 0, 1);
  if (!range) {
    char buf[32];
    snprintf(buf, 32, "Invalid address: 0x%08x", addr);
    set_error(svc, buf, 0);
    return -1;
  }
  if (!range->map) {
    set_error(svc, "Unable to map memory range: ", errno);
    return -1;
  }
  if (!range->c2creg_map) {
    set_error(svc, "Unable to map AC2C status register memory range: ", errno);
    return -1;
  }
  if (range_check(svc, range, addr, words) < 0)
    return -1;
  if (!range->start)
    return -1;

  /* Attempting to catch Bus Errors  */
  struct sigaction act;
  memset(&act,0,sizeof(act));
  act.sa_sigaction = &sigbushdl;
  act.sa_flags = SA_SIGINFO;
  if (sigaction(SIGBUS, &act, 0) < 0) {
    printf("sigaction");
    return -1;
  }
  
  /* Jump to here to avoid Bus Error */
  if (sigsetjmp(sj_env, 1)) {
    /* printf("completed jump. returning\n"); */
    return -1;
  }
  else {
    for (int i = 0; i < words; i++) {
      data[i] = REG32(range->map, addr - range->start);
    }
  }
  return 0;
}


int memsvc_write(memsvc_handle_t svc, uint32_t addr, uint32_t words, const uint32_t *data)
{
  if (memsvc_validate(svc, 1) < 0)
    return -1;

  struct memsvc_range *range = find_range(svc, addr, 1, 1);

  if (!range) {
    char buf[128];
    if ((range = find_range(svc, addr, 0, 0)) != NULL) {
      snprintf(buf, 128, "Unable to write to readonly range 0x%08x-0x%08x!  Attempted to write %u words at 0x%08x.", range->start, range->end, words, addr);
      set_error(svc, buf, 0);
      return -1;
    }
    else {
      snprintf(buf, 128, "Invalid address: 0x%08x", addr);
      set_error(svc, buf, 0);
      return -1;
    }
  }
  if (!range->map) {
    set_error(svc, "Unable to map memory range: ", errno);
    return -1;
  }
  if (!range->c2creg_map) {
    set_error(svc, "Unable to map AC2C status register memory range: ", errno);
    return -1;
  }
  if (range_check(svc, range, addr, words) < 0)
    return -1;

  for (int i = 0; i < words; i++)
    REG32(range->map, addr - range->start) = data[i];

  return 0;
}
