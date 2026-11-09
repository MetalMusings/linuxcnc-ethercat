//
//    Copyright (C) 2012 Sascha Ittner <sascha.ittner@modusoft.de>
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
//

/// @file
/// @brief Initialization code for LinuxCNC-Ethercat

#ifndef __KERNEL__
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#endif

#include "devices/lcec_generic.h"
#include "lcec.h"
#include "rtapi_app.h"
//#include <linuxcnc/rtapi_mutex.h>

MODULE_LICENSE("GPL")
MODULE_AUTHOR("Sascha Ittner <sascha.ittner@modusoft.de>")
MODULE_DESCRIPTION("Driver for EtherCAT devices")

/// @brief Global HAL Pins
static const lcec_pindesc_t master_global_pins[] = {
    {HAL_U32, HAL_OUT, offsetof(lcec_master_data_t, slaves_responding), "%s.slaves-responding"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_master_data_t, state_init), "%s.state-init"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_master_data_t, state_preop), "%s.state-preop"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_master_data_t, state_safeop), "%s.state-safeop"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_master_data_t, state_op), "%s.state-op"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_master_data_t, link_up), "%s.link-up"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_master_data_t, all_op), "%s.all-op"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_master_data_t, dc_sync_failed), "%s.dc-sync-failed"},
    {HAL_BIT, HAL_IN, offsetof(lcec_master_data_t, take_down), "%s.take-down"},
    {HAL_TYPE_UNSPECIFIED, HAL_DIR_UNSPECIFIED, -1, NULL},
};

/// @brief Master HAL pins
static const lcec_pindesc_t master_pins[] = {
#ifdef RTAPI_TASK_PLL_SUPPORT
    {HAL_S32, HAL_OUT, offsetof(lcec_master_data_t, pll_err), "%s.pll-err"},
    {HAL_S32, HAL_OUT, offsetof(lcec_master_data_t, pll_out), "%s.pll-out"},
    {HAL_U32, HAL_OUT, offsetof(lcec_master_data_t, pll_reset_cnt), "%s.pll-reset-count"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_master_data_t, dc_phased), "%s.dc-phased"},
    {HAL_S32, HAL_OUT, offsetof(lcec_master_data_t, send_offset), "%s.send-offset"},
    {HAL_U32, HAL_OUT, offsetof(lcec_master_data_t, shift_time), "%s.shift-time"},
    {HAL_S32, HAL_OUT, offsetof(lcec_master_data_t, turnaround_cycles), "%s.turnaround-cycles"},
#endif
    {HAL_TYPE_UNSPECIFIED, HAL_DIR_UNSPECIFIED, -1, NULL},
};

/// @brief Master params
static const lcec_paramdesc_t master_params[] = {
#ifdef RTAPI_TASK_PLL_SUPPORT
    {HAL_U32, HAL_RW, offsetof(lcec_master_data_t, pll_step), "%s.pll-step"},
    {HAL_U32, HAL_RW, offsetof(lcec_master_data_t, pll_max_err), "%s.pll-max-err"},
    {HAL_U32, HAL_RW, offsetof(lcec_master_data_t, dc_phase_max_err), "%s.dc-phase-max-err"},
    {HAL_S32, HAL_RW, offsetof(lcec_master_data_t, send_offset_max), "%s.send-offset-max"},
    {HAL_U32, HAL_RW, offsetof(lcec_master_data_t, auto_shift_margin), "%s.auto-shift-margin"},
    {HAL_U32, HAL_RW, offsetof(lcec_master_data_t, auto_shift_input_margin), "%s.auto-shift-input-margin"},
#endif
    {HAL_TYPE_UNSPECIFIED},
};

/// @brief Basic Slave pins
static const lcec_pindesc_t slave_pins[] = {
    {HAL_BIT, HAL_OUT, offsetof(lcec_slave_state_t, online), "%s.%s.%s.slave-online"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_slave_state_t, operational), "%s.%s.%s.slave-oper"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_slave_state_t, state_init), "%s.%s.%s.slave-state-init"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_slave_state_t, state_preop), "%s.%s.%s.slave-state-preop"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_slave_state_t, state_safeop), "%s.%s.%s.slave-state-safeop"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_slave_state_t, state_op), "%s.%s.%s.slave-state-op"},
    {HAL_BIT, HAL_OUT, offsetof(lcec_slave_state_t, dc_sync_failed), "%s.%s.%s.slave-dc-sync-failed"},
    {HAL_TYPE_UNSPECIFIED, HAL_DIR_UNSPECIFIED, -1, NULL},
};

static lcec_master_t *first_master = NULL;
static lcec_master_t *last_master = NULL;
extern int lcec_comp_id;

static lcec_master_data_t *global_hal_data;
static ec_master_state_t global_ms;

int lcec_parse_config(void);
void lcec_clear_config(void);

#ifdef __KERNEL__
static void lcec_request_lock(void *data);
static void lcec_release_lock(void *data);
#endif

lcec_master_data_t *lcec_init_master_hal(const char *pfx, int global);
lcec_slave_state_t *lcec_init_slave_state_hal(char *master_name, char *slave_name);
void lcec_update_master_hal(lcec_master_data_t *hal_data, ec_master_state_t *ms);
void lcec_update_slave_state_hal(lcec_slave_state_t *hal_data, ec_slave_config_state_t *ss);

void lcec_read_all(void *arg, long period);
void lcec_write_all(void *arg, long period);
void lcec_read_master(void *arg, long period);
void lcec_write_master(void *arg, long period);

static void sigsegv_handler(int sig);

#ifdef EC_USPACE_MASTER
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <sys/file.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <unistd.h>

static int lcec_uspace_initialized = 0;

/// @brief Path of the IPC socket for the ethercat tool, or NULL for none.
///
/// LCEC_ETHERCAT_SOCKET selects the path. Unset means the library default
/// (/var/run/ethercat.sock); "" or "none" turns the IPC server off.
static const char *lcec_uspace_socket_path(void) {
  const char *path = getenv("LCEC_ETHERCAT_SOCKET");

  if (path == NULL) {
    return EC_IPC_DEFAULT_SOCKET_PATH;
  }
  if (path[0] == 0 || strcmp(path, "none") == 0) {
    return NULL;
  }
  return path;
}

/// @brief Give the LinuxCNC user (and LCEC_ETHERCAT_SOCKET_GROUP) the socket.
///
/// The library creates the socket with mode 0660. Called with fsuid 0, so
/// the socket would be root's; hand it to the user that runs LinuxCNC, so
/// that the ethercat tool works without sudo. An extra group can be given
/// access, the same way as ec_master --socket-group does.
static void lcec_uspace_socket_access(const char *path, uid_t user) {
  const char *group_name = getenv("LCEC_ETHERCAT_SOCKET_GROUP");
  gid_t gid = (gid_t)-1;
  struct group *group;

  if (group_name != NULL && group_name[0] != 0) {
    if ((group = getgrnam(group_name)) == NULL) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "userspace master: unknown group %s for %s\n", group_name, path);
    } else {
      gid = group->gr_gid;
    }
  }
  if (chown(path, user, gid) < 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "userspace master: cannot hand %s to uid %d: %s\n", path, (int)user,
        strerror(errno));
  }
}

#ifndef EC_USPACE_LOCK_PATH_FMT
#define EC_USPACE_LOCK_PATH_FMT "/run/ethercat-uspace.%s.lock"
#endif

static int lcec_uspace_lock_fd = -1;

/// @brief True if process @p pid runs the free-running ec_master daemon.
///
/// Checked by the program file, which tells it from rtapi_app.
static int lcec_uspace_is_ec_master(pid_t pid) {
  char path[64], exe[256];
  const char *name = "/ec_master";
  ssize_t n;

  snprintf(path, sizeof(path), "/proc/%d/exe", (int)pid);
  if ((n = readlink(path, exe, sizeof(exe) - 1)) <= 0) {
    return 0;
  }
  exe[n] = 0;
  return n >= (ssize_t)strlen(name) && strcmp(exe + n - strlen(name), name) == 0;
}

/// @brief Take the interface lock that the userspace masters share.
///
/// The ec_master daemon ("ethercat start") holds EC_USPACE_LOCK_PATH_FMT
/// while it owns the interface. Such a master is asked to exit (SIGTERM),
/// and the lock is taken once it has released the interface. Anything else that holds the lock (another LinuxCNC) is an
/// error. Called with fsuid 0, since the lock is in /run.
static int lcec_uspace_take_interface(const char *interface) {
  char path[128], buf[32];
  pid_t holder = 0;
  ssize_t n;
  int fd, i;

  snprintf(path, sizeof(path), EC_USPACE_LOCK_PATH_FMT, interface);
  if ((fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644)) < 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "userspace master: cannot open %s: %s\n", path, strerror(errno));
    return -1;
  }
  if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
    if ((n = pread(fd, buf, sizeof(buf) - 1, 0)) > 0) {
      buf[n] = 0;
      holder = (pid_t)strtol(buf, NULL, 10);
    }
    if (holder <= 0 || !lcec_uspace_is_ec_master(holder)) {
      rtapi_print_msg(
          RTAPI_MSG_ERR, LCEC_MSG_PFX "userspace master: %s is in use by process %d\n", interface, (int)holder);
      close(fd);
      return -1;
    }
    rtapi_print_msg(
        RTAPI_MSG_INFO, LCEC_MSG_PFX "userspace master: stopping ec_master (pid %d) on %s\n", (int)holder, interface);
    kill(holder, SIGTERM);
    for (i = 0; flock(fd, LOCK_EX | LOCK_NB) < 0; i++) {
      if (i >= 100) {
        rtapi_print_msg(
            RTAPI_MSG_ERR, LCEC_MSG_PFX "userspace master: ec_master (pid %d) did not release %s\n", (int)holder, interface);
        close(fd);
        return -1;
      }
      usleep(100000);
    }
  }
  n = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
  if (ftruncate(fd, 0) < 0 || pwrite(fd, buf, (size_t)n, 0) != n) {
    // The lock still works without the PID; only a handover needs it.
  }
  lcec_uspace_lock_fd = fd;
  return 0;
}

/// @brief Start a master inside this process (userspace IgH master).
///
/// The kernel master gets its network interface from /etc/ethercat.conf.
/// Here it comes from the master's "interface" attribute in the XML file, or
/// else from the environment variable LCEC_ETHERCAT_INTERFACE (for example
/// enp2s0). The transport comes from the "transport" attribute, or else from
/// LCEC_ETHERCAT_TRANSPORT (default "raw").
/// Only master index 0 is supported.
static ec_master_t *lcec_request_master(lcec_master_t *master) {
  const char *interface = master->interface[0] ? master->interface : getenv("LCEC_ETHERCAT_INTERFACE");
  const char *transport_name = master->transport[0] ? master->transport : getenv("LCEC_ETHERCAT_TRANSPORT");
  ec_transport_t *transport;
  ec_master_t *ec_master;

  if (master->index != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "userspace master: only master index 0 is supported\n");
    return NULL;
  }
  if (interface == NULL || interface[0] == 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX
        "userspace master: give the EtherCAT network interface as <master interface=\"...\"> or in LCEC_ETHERCAT_INTERFACE\n");
    return NULL;
  }
  if (transport_name == NULL || transport_name[0] == 0) {
    transport_name = "raw";
  }

  if (!lcec_uspace_initialized) {
    // The IPC server lets the uspace ethercat tool reach this master.
    const char *socket_path = lcec_uspace_socket_path();
    // rtapi_app is setuid root but keeps the user's fsuid, which may not
    // create the socket in /run. Use fsuid 0 while the library makes it.
    uid_t user = setfsuid(0);
    // Before ecrt_lib_init(): it replaces the socket of an ec_master daemon.
    if (lcec_uspace_take_interface(interface) < 0) {
      setfsuid(user);
      return NULL;
    }
    int ret = ecrt_lib_init(NULL, socket_path);
    if (ret >= 0 && socket_path != NULL) {
      lcec_uspace_socket_access(socket_path, user);
    }
    setfsuid(user);
    if (ret < 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "userspace master: ecrt_lib_init() failed\n");
      return NULL;
    }
    lcec_uspace_initialized = 1;
  }

  if ((transport = ec_transport_create_by_name(transport_name, interface)) == NULL) {
    rtapi_print_msg(
        RTAPI_MSG_ERR, LCEC_MSG_PFX "userspace master: cannot create transport %s on %s\n", transport_name, interface);
    return NULL;
  }
  if ((ec_master = ecrt_startup_master(master->index, transport, NULL, 0, -1, -1)) == NULL) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "userspace master: cannot start master on %s\n", interface);
    ec_transport_destroy(transport);
    return NULL;
  }
  rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "userspace master started on %s (%s)\n", interface, transport_name);
  return ec_master;
}

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <time.h>

/// Helper thread that runs ecrt_master_activate() outside the servo thread.
///
/// In the userspace master, activation stops the master's idle thread
/// (pthread_join), starts the operation thread and may sleep 10 ms for an
/// outstanding FSM datagram. Done in the servo thread, that stalls it for
/// 11-13 ms. The servo thread still makes the first, period-anchored
/// ecrt_master_application_time() call (it sets IgH's dc_ref_time, the
/// SYNC0 phase anchor) and then only posts a semaphore.
typedef struct {
  pthread_t thread;
  sem_t sem;
  int quit;
} lcec_activator_t;

static void *lcec_activator_main(void *arg) {
  lcec_master_t *master = (lcec_master_t *)arg;
  lcec_activator_t *act = (lcec_activator_t *)master->activator;
  long long t0;

  while (sem_wait(&act->sem) != 0 && errno == EINTR) {
  }
  if (act->quit) {
    return NULL;
  }

  t0 = rtapi_get_time();
  if (ecrt_master_activate(master->master)) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failed to activate master %s\n", master->name);
    return NULL;  // activated stays 0, so the servo thread keeps skipping this master
  }
  master->process_data = ecrt_domain_data(master->domain);
  master->process_data_len = ecrt_domain_size(master->domain);
  rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "master %s activated in %lld us (helper thread)\n", master->name, (rtapi_get_time() - t0) / 1000);

  __atomic_store_n(&master->activated, 1, __ATOMIC_RELEASE);
  return NULL;
}

static int lcec_start_activator(lcec_master_t *master) {
  lcec_activator_t *act = calloc(1, sizeof(*act));
  pthread_attr_t attr;
  struct sched_param param = {0};
  int ret;

  if (act == NULL || sem_init(&act->sem, 0, 0) != 0) {
    free(act);
    return -1;
  }
  master->activator = act;
  pthread_attr_init(&attr);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
  pthread_attr_setschedparam(&attr, &param);
  ret = pthread_create(&act->thread, &attr, lcec_activator_main, master);
  pthread_attr_destroy(&attr);
  if (ret != 0) {
    sem_destroy(&act->sem);
    free(act);
    master->activator = NULL;
    return -1;
  }
  return 0;
}

static void lcec_stop_activator(lcec_master_t *master) {
  lcec_activator_t *act = (lcec_activator_t *)master->activator;

  if (act == NULL) {
    return;
  }
  act->quit = 1;
  sem_post(&act->sem);  // no effect if the thread already took the semaphore
  pthread_join(act->thread, NULL);
  sem_destroy(&act->sem);
  free(act);
  master->activator = NULL;
}

/// Deactivate a userspace master and wait until its slaves are in PREOP.
///
/// The kernel master keeps running after LinuxCNC releases it, and its idle
/// thread takes the slaves back to PREOP. The userspace master ends with
/// LinuxCNC, so without this the slaves lose their frames in OP and drop to
/// SAFEOP + ERROR (sync error, SM watchdog). ecrt_master_deactivate() requests
/// PREOP and restarts the idle thread; give it time before the release.
/// Called from rtapi_app_exit(), outside the servo thread.
static void lcec_uspace_deactivate(lcec_master_t *master) {
  const struct timespec period = {0, 10 * 1000 * 1000};  // 10 ms
  const int max_polls = 200;                             // 2 s
  ec_master_state_t ms;
  int i;

  if (!__atomic_exchange_n(&master->activated, 0, __ATOMIC_ACQ_REL)) {
    return;  // never activated: the slaves were not taken past PREOP
  }

  // The servo thread now skips this master. Normally it is already stopped
  // ("halcmd stop" comes before unloading); otherwise let a cycle in
  // progress finish before the domain memory goes away.
  nanosleep(&period, NULL);

  ecrt_master_deactivate(master->master);

  memset(&ms, 0, sizeof(ms));
  for (i = 0; i < max_polls; i++) {
    nanosleep(&period, NULL);
    ecrt_master_state(master->master, &ms);
    if (ms.al_states == 0x02) {  // every responding slave is in PREOP
      rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "master %s: slaves in PREOP after %d ms\n", master->name, (i + 1) * 10);
      return;
    }
  }
  rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "master %s: slaves not in PREOP after %d ms (AL states 0x%02x)\n", master->name,
      max_polls * 10, ms.al_states);
}
#else
static ec_master_t *lcec_request_master(lcec_master_t *master) {
  if (master->interface[0]) {
    rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "master %s: interface=\"%s\" is only used by lcec_uspace; the kernel master takes its interface from ethercat.conf\n",
        master->name, master->interface);
  }
  if (master->transport[0]) {
    rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "master %s: transport=\"%s\" is only used by lcec_uspace\n",
        master->name, master->transport);
  }
  return ecrt_request_master(master->index);
}
#endif

/// @brief Main entrypoint from LinuxCNC
int rtapi_app_main(void) {
  int slave_count;
  lcec_master_t *master;
  lcec_slave_t *slave;
  char name[HAL_NAME_LEN + 1];
  lcec_slave_sdoconf_t *sdo_config;
  lcec_slave_idnconf_t *idn_config;
  struct timeval tv;
  int pdo_entry_count = 0;

#ifndef __KERNEL
  struct sigaction handler;
  sigemptyset(&handler.sa_mask);
  handler.sa_flags = SA_NODEFER | SA_RESETHAND;
  handler.sa_handler = sigsegv_handler;

  sigaction(SIGSEGV, &handler, NULL);
  sigaction(SIGBUS, &handler, NULL);
  sigaction(SIGFPE, &handler, NULL);
  sigaction(SIGKILL, &handler, NULL);
#endif

  // connect to the HAL
  if ((lcec_comp_id = hal_init(LCEC_COMP_NAME)) < 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "hal_init() failed\n");
    goto fail0;
  }

  // parse configuration
  if ((slave_count = lcec_parse_config()) < 0) {
    goto fail1;
  }

  // init global hal data
  if ((global_hal_data = lcec_init_master_hal(LCEC_MODULE_NAME, 1)) == NULL) {
    goto fail2;
  }

  // initialize masters
  for (master = first_master; master != NULL; master = master->next) {
    // request ethercat master
    if (!(master->master = lcec_request_master(master))) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "requesting master %s (index %d) failed\n", master->name, master->index);
      goto fail2;
    }
#ifdef EC_USPACE_MASTER
    if (lcec_start_activator(master) != 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "master %s: cannot start the activation thread\n", master->name);
      goto fail2;
    }
#endif

#ifdef __KERNEL__
    // register callbacks
    ecrt_master_callbacks(master->master, lcec_request_lock, lcec_release_lock, master);
#endif

    // create domain
    if (!(master->domain = ecrt_master_create_domain(master->master))) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "master %s domain creation failed\n", master->name);
      goto fail2;
    }

    // initialize slaves
    for (slave = master->first_slave; slave != NULL; slave = slave->next) {
      // read slave config

      rtapi_print_msg(RTAPI_MSG_DBG, LCEC_MSG_PFX "calling ecrt_master_slave_config for slave %s.%s\n", master->name, slave->name);
      if (!(slave->config = ecrt_master_slave_config(master->master, 0, slave->index, slave->vid, slave->pid))) {
        rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "fail to read slave %s.%s configuration\n", master->name, slave->name);
        goto fail2;
      }

      // initialize sdos
      if (slave->sdo_config != NULL) {
        for (sdo_config = slave->sdo_config; sdo_config->index != 0xffff;
             sdo_config = (lcec_slave_sdoconf_t *)&sdo_config->data[sdo_config->length]) {
          if (sdo_config->subindex == LCEC_CONF_SDO_COMPLETE_SUBIDX) {
            if (ecrt_slave_config_complete_sdo(slave->config, sdo_config->index, &sdo_config->data[0], sdo_config->length) != 0) {
              rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failed to configure slave %s.%s sdo %04x (complete)\n", master->name,
                  slave->name, sdo_config->index);
            }
          } else {
            if (lcec_write_sdo(slave, sdo_config->index, sdo_config->subindex, &sdo_config->data[0], sdo_config->length) != 0) {
              rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failed to configure slave %s.%s sdo %04x:%02x\n", master->name, slave->name,
                  sdo_config->index, sdo_config->subindex);
            }
          }
        }
      }

      // initialize idns
      if (slave->idn_config != NULL) {
        for (idn_config = slave->idn_config; idn_config->state != 0;
             idn_config = (lcec_slave_idnconf_t *)&idn_config->data[idn_config->length]) {
          if (ecrt_slave_config_idn(
                  slave->config, idn_config->drive, idn_config->idn, idn_config->state, &idn_config->data[0], idn_config->length) != 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "fail to configure slave %s.%s drive %d idn %c-%d-%d (state %d, length %u)\n",
                master->name, slave->name, idn_config->drive, (idn_config->idn & 0x8000) ? 'P' : 'S', (idn_config->idn >> 12) & 0x0007,
                idn_config->idn & 0x0fff, idn_config->state, (unsigned int)idn_config->length);
          }
        }
      }

      slave->regs = lcec_allocate_pdo_entry_reg(LCEC_MAX_PDO_REG_COUNT);
      if (slave->regs == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failure allocating PDO entries for slave %s.%s\n", master->name, slave->name);
        goto fail2;
      }

      // setup pdos
      if (slave->proc_init != NULL) {
        rtapi_print_msg(RTAPI_MSG_DBG, LCEC_MSG_PFX "proc_init for slave %s.%s\n", master->name, slave->name);
        if ((slave->proc_init(lcec_comp_id, slave)) != 0) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failure in proc_init for slave %s.%s\n", master->name, slave->name);
          goto fail2;
        }
      }

      // configure dc for this slave
      if (slave->dc_conf != NULL) {
        ecrt_slave_config_dc(slave->config, slave->dc_conf->assignActivate, slave->dc_conf->sync0Cycle, slave->dc_conf->sync0Shift,
            slave->dc_conf->sync1Cycle, slave->dc_conf->sync1Shift);
        rtapi_print_msg(RTAPI_MSG_DBG,
            LCEC_MSG_PFX "configuring DC for slave %s.%s: assignActivate=x%x sync0Cycle=%d sync0Shift=%d sync1Cycle=%d sync1Shift=%d\n",
            master->name, slave->name, slave->dc_conf->assignActivate, slave->dc_conf->sync0Cycle, slave->dc_conf->sync0Shift,
            slave->dc_conf->sync1Cycle, slave->dc_conf->sync1Shift);
        // SYNC0 can only keep a fixed phase to the servo thread if one period is a multiple of the other
        if (slave->dc_conf->sync0Cycle > 0 && master->app_time_period > 0 &&
            (slave->dc_conf->sync0Cycle % master->app_time_period) != 0 &&
            (master->app_time_period % slave->dc_conf->sync0Cycle) != 0) {
          rtapi_print_msg(RTAPI_MSG_WARN,
              LCEC_MSG_PFX "slave %s.%s: sync0Cycle=%u is not a multiple or divisor of appTimePeriod=%u, SYNC0 phase to the servo thread will drift\n",
              master->name, slave->name, slave->dc_conf->sync0Cycle, master->app_time_period);
        }
        // Where SYNC0 fires in the servo thread period (only if SYNC0 is activated, 0x0981 bit 1).
        // The master's globalShiftTime is in app_start_time, and the EtherCAT master adds this
        // slave's sync0Shift to the start time in 0x0990 (see documentation/distributed-clocks.md).
        if ((slave->dc_conf->assignActivate & 0x0200) != 0) {
          long long sync0_pos = (long long)master->global_shift_time + slave->dc_conf->sync0Shift;
          rtapi_print_msg(RTAPI_MSG_INFO,
              LCEC_MSG_PFX "slave %s.%s: SYNC0 at globalShiftTime + sync0Shift = %u + %d = %lld ns after the servo period start\n",
              master->name, slave->name, master->global_shift_time, slave->dc_conf->sync0Shift, sync0_pos);
          if (slave->dc_conf->sync0Cycle > 0 && (sync0_pos < 0 || sync0_pos >= (long long)slave->dc_conf->sync0Cycle)) {
            rtapi_print_msg(RTAPI_MSG_WARN,
                LCEC_MSG_PFX "slave %s.%s: globalShiftTime + sync0Shift = %lld ns is outside 0..sync0Cycle (%u ns), SYNC0 falls in another "
                             "cycle. Is the same shift given in both globalShiftTime and sync0Shift?\n",
                master->name, slave->name, sync0_pos, slave->dc_conf->sync0Cycle);
          }
        }
      }

      // Configure the slave's watchdog times.
      if (slave->wd_conf != NULL) {
        ecrt_slave_config_watchdog(slave->config, slave->wd_conf->divider, slave->wd_conf->intervals);
      }

      // configure slave
      if (slave->sync_info != NULL) {
        rtapi_print_msg(RTAPI_MSG_DBG, LCEC_MSG_PFX "sync_info setup for slave %s.%s\n", master->name, slave->name);
        if (ecrt_slave_config_pdos(slave->config, EC_END, slave->sync_info)) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "fail to configure slave %s.%s\n", master->name, slave->name);
          goto fail2;
        }
      }

      // export state pins
      rtapi_print_msg(RTAPI_MSG_DBG, LCEC_MSG_PFX "init slave state hal for slave %s.%s\n", master->name, slave->name);
      if ((slave->hal_state_data = lcec_init_slave_state_hal(master->name, slave->name)) == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failure to export slave pins for slave %s.%s\n", master->name, slave->name);
        goto fail2;
      }

      pdo_entry_count += lcec_pdo_entry_reg_len(slave->regs);
    }

    lcec_pdo_entry_reg_t *master_regs = lcec_allocate_pdo_entry_reg(pdo_entry_count + 1);
    for (slave = master->first_slave; slave != NULL; slave = slave->next) {
      if (lcec_append_pdo_entry_reg(master_regs, slave->regs) < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failure to append PDO entries for slave %s.%s\n", master->name, slave->name);
        goto fail2;
      }
    }

    // register PDO entries
    rtapi_print_msg(RTAPI_MSG_DBG, LCEC_MSG_PFX "register PDO entries\n");
    if (ecrt_domain_reg_pdo_entry_list(master->domain, master_regs->pdo_entry_regs)) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "master %s PDO entry registration failed\n", master->name);
      goto fail2;
    }

    // Mark master as not yet activated (will be activated on first read/write call)
    master->activated = 0;
    master->process_data = NULL;
    master->process_data_len = 0;

    // init hal data
    rtapi_snprintf(name, HAL_NAME_LEN, "%s.%s", LCEC_MODULE_NAME, master->name);
    if ((master->hal_data = lcec_init_master_hal(name, 0)) == NULL) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failure to init hal pins for slave %s.%s\n", master->name, slave->name);
      goto fail2;
    }

#ifdef RTAPI_TASK_PLL_SUPPORT
    // set default PLL_STEP: use +/-0.1% of period
    master->hal_data->pll_step = master->app_time_period / 1000;
    // set default PLL_MAX_ERR: one period
    master->hal_data->pll_max_err = master->app_time_period;
    // set default DC_PHASE_MAX_ERR: 10% of period
    master->hal_data->dc_phase_max_err = master->app_time_period / 10;
    master->hal_data->auto_shift_margin = LCEC_AUTOSHIFT_MARGIN_DEFAULT;
    master->hal_data->auto_shift_input_margin = LCEC_AUTOSHIFT_INPUT_MARGIN_DEFAULT;
    *(master->hal_data->shift_time) = master->global_shift_time;
    if (master->auto_shift) {
      lcec_autoshift_init(&master->autoshift, master->app_time_period, master->global_shift_time);
    }
#else
    if (master->auto_shift) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "master %s: globalShiftTime=\"auto\" needs RTAPI_TASK_PLL_SUPPORT\n", master->name);
      goto fail2;
    }
#endif

    // export read function
    rtapi_snprintf(name, HAL_NAME_LEN, "%s.%s.read", LCEC_MODULE_NAME, master->name);
    if (hal_export_funct(name, lcec_read_master, master, 0, 0, lcec_comp_id) != 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "master %s read funct export failed\n", master->name);
      goto fail2;
    }
    // export write function
    rtapi_snprintf(name, HAL_NAME_LEN, "%s.%s.write", LCEC_MODULE_NAME, master->name);
    if (hal_export_funct(name, lcec_write_master, master, 0, 0, lcec_comp_id) != 0) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "master %s write funct export failed\n", master->name);
      goto fail2;
    }
  }

  // export read-all function
  rtapi_snprintf(name, HAL_NAME_LEN, "%s.read-all", LCEC_MODULE_NAME);
  if (hal_export_funct(name, lcec_read_all, NULL, 0, 0, lcec_comp_id) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "read-all funct export failed\n");
    goto fail2;
  }
  // export write-all function
  rtapi_snprintf(name, HAL_NAME_LEN, "%s.write-all", LCEC_MODULE_NAME);
  if (hal_export_funct(name, lcec_write_all, NULL, 0, 0, lcec_comp_id) != 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "write-all funct export failed\n");
    goto fail2;
  }

  rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "installed driver for %d slaves\n", slave_count);
  hal_ready(lcec_comp_id);
  return 0;

fail2:
  rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failure, clearing config\n");
  lcec_clear_config();
fail1:
  rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "exiting\n");
  hal_exit(lcec_comp_id);
fail0:
  rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "returning -EINVAL\n");
  return -EINVAL;
}

/// @brief Shut down LinuxCNC-Ethercat
void rtapi_app_exit(void) {
  lcec_master_t *master;

  // deactivate all masters
  for (master = first_master; master != NULL; master = master->next) {
#ifdef EC_USPACE_MASTER
    lcec_stop_activator(master);  // lets an activation in progress finish first
    lcec_uspace_deactivate(master);
#else
    ecrt_master_deactivate(master->master);
#endif
  }

  lcec_clear_config();
  hal_exit(lcec_comp_id);
}

/// @brief Parse configuration from `lcec_conf`.
int lcec_parse_config(void) {
  int shmem_id;
  void *shmem_ptr;
  LCEC_CONF_HEADER_T *header;
  size_t length;
  char *conf;
  int slave_count;
  const lcec_typelist_t *type;
  lcec_master_t *master;
  lcec_slave_t *slave;
  lcec_slave_dc_t *dc;
  lcec_slave_watchdog_t *wd;
  LCEC_CONF_TYPE_T conf_type;
  LCEC_CONF_MASTER_T *master_conf;
  LCEC_CONF_SLAVE_T *slave_conf;
  LCEC_CONF_DC_T *dc_conf;
  LCEC_CONF_WATCHDOG_T *wd_conf;
  LCEC_CONF_SYNCMANAGER_T *sm_conf;
  LCEC_CONF_PDO_T *pdo_conf;
  LCEC_CONF_PDOENTRY_T *pe_conf;
  LCEC_CONF_COMPLEXENTRY_T *ce_conf;
  LCEC_CONF_SDOCONF_T *sdo_conf;
  LCEC_CONF_IDNCONF_T *idn_conf;
  LCEC_CONF_MODPARAM_T *modparam_conf;
  ec_pdo_entry_info_t *generic_pdo_entries;
  ec_pdo_info_t *generic_pdos;
  ec_sync_info_t *generic_sync_managers;
  lcec_generic_pin_t *generic_hal_data;
  hal_pin_dir_t generic_hal_dir;
  lcec_slave_sdoconf_t *sdo_config;
  lcec_slave_idnconf_t *idn_config;
  lcec_slave_modparam_t *modparams;

  // initialize list
  first_master = NULL;
  last_master = NULL;

  // try to get config header
  shmem_id = rtapi_shmem_new(LCEC_CONF_SHMEM_KEY, lcec_comp_id, sizeof(LCEC_CONF_HEADER_T));
  if (shmem_id < 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "couldn't allocate user/RT shared memory\n");
    goto fail0;
  }
  if (lcec_rtapi_shmem_getptr(shmem_id, &shmem_ptr) < 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "couldn't map user/RT shared memory\n");
    goto fail1;
  }

  // check magic, get length and close shmem
  header = (LCEC_CONF_HEADER_T *)shmem_ptr;
  if (header->magic != LCEC_CONF_SHMEM_MAGIC) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "lcec_conf is not loaded\n");
    goto fail1;
  }
  length = header->length;
  rtapi_shmem_delete(shmem_id, lcec_comp_id);

  // reopen shmem with proper size
  shmem_id = rtapi_shmem_new(LCEC_CONF_SHMEM_KEY, lcec_comp_id, sizeof(LCEC_CONF_HEADER_T) + length);
  if (shmem_id < 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "couldn't allocate user/RT shared memory\n");
    goto fail0;
  }
  if (lcec_rtapi_shmem_getptr(shmem_id, &shmem_ptr) < 0) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "couldn't map user/RT shared memory\n");
    goto fail1;
  }

  // get pointer to config
  conf = ((char *)shmem_ptr + sizeof(LCEC_CONF_HEADER_T));

  // process config items
  slave_count = 0;
  master = NULL;
  slave = NULL;
  generic_pdo_entries = NULL;
  generic_pdos = NULL;
  generic_sync_managers = NULL;
  generic_hal_data = NULL;
  generic_hal_dir = HAL_DIR_UNSPECIFIED;
  sdo_config = NULL;
  idn_config = NULL;
  pe_conf = NULL;
  modparams = NULL;
  while ((conf_type = ((LCEC_CONF_NULL_T *)conf)->confType) != lcecConfTypeNone) {
    // get type
    switch (conf_type) {
      case lcecConfTypeMaster:
        // get config token
        master_conf = (LCEC_CONF_MASTER_T *)conf;
        conf += sizeof(LCEC_CONF_MASTER_T);

        // alloc master memory
        master = LCEC_ALLOCATE(lcec_master_t);

        // initialize master
        master->index = master_conf->index;
        strncpy(master->name, master_conf->name, LCEC_CONF_STR_MAXLEN);
        master->name[LCEC_CONF_STR_MAXLEN - 1] = 0;
        strncpy(master->interface, master_conf->interface, LCEC_CONF_STR_MAXLEN);
        master->interface[LCEC_CONF_STR_MAXLEN - 1] = 0;
        strncpy(master->transport, master_conf->transport, LCEC_CONF_STR_MAXLEN);
        master->transport[LCEC_CONF_STR_MAXLEN - 1] = 0;
        master->app_time_period = master_conf->appTimePeriod;
        master->sync_ref_cycles = master_conf->refClockSyncCycles;
        // sync_to_ref_clock controls clock sync direction:
        //   false: master is clock source, DC syncs to master (default)
        //   true: DC is clock source, servo thread syncs to DC via PLL
        // Note: legacy mode - negative refClockSyncCycles enables sync_to_ref_clock
        master->sync_to_ref_clock = master_conf->syncToRefClock;
        master->global_shift_time = master_conf->globalShiftTime;
        // globalShiftTime="auto": start in the middle of the period, and
        // choose once the frames' timing has been measured
        master->auto_shift = master_conf->autoShift;
        if (master->auto_shift) {
          master->global_shift_time = master->app_time_period / 2;
        }

        // add master to list
        LCEC_LIST_APPEND(first_master, last_master, master);
        break;

      case lcecConfTypeSlave:
        // get config token
        slave_conf = (LCEC_CONF_SLAVE_T *)conf;
        conf += sizeof(LCEC_CONF_SLAVE_T);

        // check for master
        if (master == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "Master node for slave missing\n");
          goto fail2;
        }

        // check for valid slave type
        if (!strcmp(slave_conf->type_name, "generic")) {
          type = NULL;
        } else {
          type = lcec_findslavetype(slave_conf->type_name);

          if (type == NULL) {
            rtapi_print_msg(RTAPI_MSG_WARN, LCEC_MSG_PFX "Invalid slave name \"%s\"\n", slave_conf->type_name);
            continue;
          }
        }

        // create new slave
        slave = LCEC_ALLOCATE(lcec_slave_t);

        // initialize slave
        generic_pdo_entries = NULL;
        generic_pdos = NULL;
        generic_sync_managers = NULL;
        generic_hal_data = NULL;
        generic_hal_dir = HAL_DIR_UNSPECIFIED;
        sdo_config = NULL;
        idn_config = NULL;
        modparams = NULL;

        slave->index = slave_conf->index;
        strncpy(slave->name, slave_conf->name, LCEC_CONF_STR_MAXLEN);
        slave->name[LCEC_CONF_STR_MAXLEN - 1] = 0;
        slave->master = master;

        // add slave to list
        LCEC_LIST_APPEND(master->first_slave, master->last_slave, slave);

        if (type != NULL) {
          // normal slave
          if (slave_conf->vid)
            slave->vid = slave_conf->vid;
          else
            slave->vid = type->vid;

          if (slave_conf->pid)
            slave->pid = slave_conf->pid;
          else
            slave->pid = type->pid;

          slave->is_fsoe_logic = type->is_fsoe_logic;
          slave->proc_preinit = type->proc_preinit;
          slave->proc_init = type->proc_init;
          slave->flags = type->flags;
        } else {
          // generic slave
          slave->vid = slave_conf->vid;
          slave->pid = slave_conf->pid;
          slave->generic_pdo_entry_count = slave_conf->pdoMappingCount;
          slave->proc_init = lcec_generic_init;

          // alloc hal memory
          if ((generic_hal_data = LCEC_HAL_ALLOCATE_ARRAY(lcec_generic_pin_t, slave_conf->pdoMappingCount)) == NULL) {
            rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "hal_malloc() for slave %s.%s failed\n", master->name, slave_conf->name);
            goto fail2;
          }
          memset(generic_hal_data, 0, sizeof(lcec_generic_pin_t) * slave_conf->pdoMappingCount);

          // alloc pdo entry memory
          generic_pdo_entries = LCEC_ALLOCATE_ARRAY(ec_pdo_entry_info_t, slave_conf->pdoEntryCount);

          // alloc pdo memory
          generic_pdos = LCEC_ALLOCATE_ARRAY(ec_pdo_info_t, slave_conf->pdoCount);

          // alloc sync manager memory
          generic_sync_managers = LCEC_ALLOCATE_ARRAY(ec_sync_info_t, (slave_conf->syncManagerCount + 1));

          generic_sync_managers->index = 0xff;
        }

        // alloc sdo config memory
        if (slave_conf->sdoConfigLength > 0) {
          sdo_config = (lcec_slave_sdoconf_t *)lcec_zalloc(slave_conf->sdoConfigLength + sizeof(lcec_slave_sdoconf_t));
          if (sdo_config == NULL) {
            rtapi_print_msg(
                RTAPI_MSG_ERR, LCEC_MSG_PFX "Unable to allocate slave %s.%s sdo entry memory\n", master->name, slave_conf->name);
            goto fail2;
          }
        }

        // alloc idn config memory
        if (slave_conf->idnConfigLength > 0) {
          idn_config = (lcec_slave_idnconf_t *)lcec_zalloc(slave_conf->idnConfigLength + sizeof(lcec_slave_idnconf_t));
          if (idn_config == NULL) {
            rtapi_print_msg(
                RTAPI_MSG_ERR, LCEC_MSG_PFX "Unable to allocate slave %s.%s idn entry memory\n", master->name, slave_conf->name);
            goto fail2;
          }
        }

        // alloc modparam memory
        if (slave_conf->modParamCount > 0) {
          modparams = LCEC_ALLOCATE_ARRAY(lcec_slave_modparam_t, (slave_conf->modParamCount + 1));
          modparams[slave_conf->modParamCount].id = -1;
        }

        slave->hal_data = generic_hal_data;
        slave->generic_pdo_entries = generic_pdo_entries;
        slave->generic_pdos = generic_pdos;
        slave->generic_sync_managers = generic_sync_managers;
        if (slave_conf->configPdos) {
          slave->sync_info = generic_sync_managers;
        }
        slave->sdo_config = sdo_config;
        slave->idn_config = idn_config;
        slave->modparams = modparams;
        slave->dc_conf = NULL;
        slave->wd_conf = NULL;

        // update slave count
        slave_count++;
        break;

      case lcecConfTypeDcConf:
        // get config token
        dc_conf = (LCEC_CONF_DC_T *)conf;
        conf += sizeof(LCEC_CONF_DC_T);

        // check for slave
        if (slave == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "Slave node for dc config missing\n");
          goto fail2;
        }

        // check for double dc config
        if (slave->dc_conf != NULL) {
          rtapi_print_msg(RTAPI_MSG_WARN, LCEC_MSG_PFX "Double dc config for slave %s.%s\n", master->name, slave->name);
          continue;
        }

        // create new dc config
        dc = LCEC_ALLOCATE(lcec_slave_dc_t);

        // initialize dc conf
        dc->assignActivate = dc_conf->assignActivate;
        dc->sync0Cycle = dc_conf->sync0Cycle;
        dc->sync0Shift = dc_conf->sync0Shift;
        dc->sync1Cycle = dc_conf->sync1Cycle;
        dc->sync1Shift = dc_conf->sync1Shift;

        // add to slave
        slave->dc_conf = dc;
        break;

      case lcecConfTypeWatchdog:
        // get config token
        wd_conf = (LCEC_CONF_WATCHDOG_T *)conf;
        conf += sizeof(LCEC_CONF_WATCHDOG_T);

        // check for slave
        if (slave == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "Slave node for watchdog config missing\n");
          goto fail2;
        }

        // check for double wd config
        if (slave->wd_conf != NULL) {
          rtapi_print_msg(RTAPI_MSG_WARN, LCEC_MSG_PFX "Double watchdog config for slave %s.%s\n", master->name, slave->name);
          continue;
        }

        // create new wd config
        wd = LCEC_ALLOCATE(lcec_slave_watchdog_t);

        // initialize wd conf
        wd->divider = wd_conf->divider;
        wd->intervals = wd_conf->intervals;

        // add to slave
        slave->wd_conf = wd;
        break;

      case lcecConfTypeSyncManager:
        // get config token
        sm_conf = (LCEC_CONF_SYNCMANAGER_T *)conf;
        conf += sizeof(LCEC_CONF_SYNCMANAGER_T);

        // check for syncmanager
        if (generic_sync_managers == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "Sync manager for generic device missing\n");
          goto fail2;
        }

        // check for pdos
        if (generic_pdos == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "PDOs for generic device missing\n");
          goto fail2;
        }

        // initialize sync manager
        generic_sync_managers->index = sm_conf->index;
        generic_sync_managers->dir = sm_conf->dir;
        generic_sync_managers->n_pdos = sm_conf->pdoCount;
        generic_sync_managers->pdos = sm_conf->pdoCount == 0 ? NULL : generic_pdos;

        // get hal direction
        switch (sm_conf->dir) {
          case EC_DIR_INPUT:
            generic_hal_dir = HAL_OUT;
            break;
          case EC_DIR_OUTPUT:
            generic_hal_dir = HAL_IN;
            break;
          default:
            generic_hal_dir = HAL_DIR_UNSPECIFIED;
        }

        // next syncmanager
        generic_sync_managers++;
        generic_sync_managers->index = 0xff;
        break;

      case lcecConfTypePdo:
        // get config token
        pdo_conf = (LCEC_CONF_PDO_T *)conf;
        conf += sizeof(LCEC_CONF_PDO_T);

        // check for pdos
        if (generic_pdos == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "PDOs for generic device missing\n");
          goto fail2;
        }

        // check for pdos entries
        if (generic_pdo_entries == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "PDO entries for generic device missing\n");
          goto fail2;
        }

        // initialize pdo
        generic_pdos->index = pdo_conf->index;
        generic_pdos->n_entries = pdo_conf->pdoEntryCount;
        generic_pdos->entries = pdo_conf->pdoEntryCount == 0 ? NULL : generic_pdo_entries;

        // next pdo
        generic_pdos++;
        break;

      case lcecConfTypePdoEntry:
        // get config token
        pe_conf = (LCEC_CONF_PDOENTRY_T *)conf;
        conf += sizeof(LCEC_CONF_PDOENTRY_T);

        // check for pdos entries
        if (generic_pdo_entries == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "PDO entries for generic device missing\n");
          goto fail2;
        }

        // check for hal data
        if (generic_hal_data == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "HAL data for generic device missing\n");
          goto fail2;
        }

        // check for hal dir
        if (generic_hal_dir == HAL_DIR_UNSPECIFIED) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "HAL direction for generic device missing\n");
          goto fail2;
        }

        // initialize pdo entry
        generic_pdo_entries->index = pe_conf->index;
        generic_pdo_entries->subindex = pe_conf->subindex;
        generic_pdo_entries->bit_length = pe_conf->bitLength;

        // initialize hal data
        if (pe_conf->halPin[0] != 0) {
          strncpy(generic_hal_data->name, pe_conf->halPin, LCEC_CONF_STR_MAXLEN);
          generic_hal_data->name[LCEC_CONF_STR_MAXLEN - 1] = 0;
          generic_hal_data->type = pe_conf->halType;
          generic_hal_data->subType = pe_conf->subType;
          generic_hal_data->floatScale = pe_conf->floatScale;
          generic_hal_data->floatOffset = pe_conf->floatOffset;
          generic_hal_data->bitOffset = 0;
          generic_hal_data->bitLength = pe_conf->bitLength;
          generic_hal_data->dir = generic_hal_dir;
          generic_hal_data->pdo_idx = pe_conf->index;
          generic_hal_data->pdo_sidx = pe_conf->subindex;
          generic_hal_data++;
        }

        // next pdo entry
        generic_pdo_entries++;
        break;

      case lcecConfTypeComplexEntry:
        // get config token
        ce_conf = (LCEC_CONF_COMPLEXENTRY_T *)conf;
        conf += sizeof(LCEC_CONF_COMPLEXENTRY_T);

        // check for pdoEntry
        if (pe_conf == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "pdoEntry for generic device missing\n");
          goto fail2;
        }

        // check for hal data
        if (generic_hal_data == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "HAL data for generic device missing\n");
          goto fail2;
        }

        // initialize hal data
        if (ce_conf->halPin[0] != 0) {
          strncpy(generic_hal_data->name, ce_conf->halPin, LCEC_CONF_STR_MAXLEN);
          generic_hal_data->name[LCEC_CONF_STR_MAXLEN - 1] = 0;
          generic_hal_data->type = ce_conf->halType;
          generic_hal_data->subType = ce_conf->subType;
          generic_hal_data->floatScale = ce_conf->floatScale;
          generic_hal_data->floatOffset = ce_conf->floatOffset;
          generic_hal_data->bitOffset = ce_conf->bitOffset;
          generic_hal_data->bitLength = ce_conf->bitLength;
          generic_hal_data->dir = generic_hal_dir;
          generic_hal_data->pdo_idx = pe_conf->index;
          generic_hal_data->pdo_sidx = pe_conf->subindex;
          generic_hal_data++;
        }
        break;

      case lcecConfTypeSdoConfig:
        // get config token
        sdo_conf = (LCEC_CONF_SDOCONF_T *)conf;
        conf += sizeof(LCEC_CONF_SDOCONF_T) + sdo_conf->length;

        if (sdo_config == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "sdo_config is NULL\n");
          goto fail2;
        }

        // copy attributes
        sdo_config->index = sdo_conf->index;
        sdo_config->subindex = sdo_conf->subindex;
        sdo_config->length = sdo_conf->length;

        // copy data
        memcpy(sdo_config->data, sdo_conf->data, sdo_config->length);

        sdo_config = (lcec_slave_sdoconf_t *)&sdo_config->data[sdo_config->length];
        sdo_config->index = 0xffff;
        break;

      case lcecConfTypeIdnConfig:
        // get config token
        idn_conf = (LCEC_CONF_IDNCONF_T *)conf;
        conf += sizeof(LCEC_CONF_IDNCONF_T) + idn_conf->length;

        if (idn_config == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "idn_config is NULL\n");
          goto fail2;
        }

        // copy attributes
        idn_config->drive = idn_conf->drive;
        idn_config->idn = idn_conf->idn;
        idn_config->state = idn_conf->state;
        idn_config->length = idn_conf->length;

        // copy data
        memcpy(idn_config->data, idn_conf->data, idn_config->length);

        idn_config = (lcec_slave_idnconf_t *)&idn_config->data[idn_config->length];
        break;

      case lcecConfTypeModParam:
        // get config token
        modparam_conf = (LCEC_CONF_MODPARAM_T *)conf;
        conf += sizeof(LCEC_CONF_MODPARAM_T);

        // check for slave
        if (slave == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "Slave node for modparam config missing\n");
          goto fail2;
        }

        if (modparams == NULL) {
          rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "modparams is nullg\n");
          goto fail2;
        }

        // copy attributes
        modparams->id = modparam_conf->id;
        modparams->value = modparam_conf->value;
        modparams->name = modparam_conf->name;

        // next entry
        modparams++;
        break;

      default:
        rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "Unknown config item type\n");
        goto fail2;
    }
  }

  // close shmem
  rtapi_shmem_delete(shmem_id, lcec_comp_id);

  // allocate PDO entity memory
  for (master = first_master; master != NULL; master = master->next) {
    // stage 1 preinit: process all but FSOE logic devices
    for (slave = master->first_slave; slave != NULL; slave = slave->next) {
      if (!slave->is_fsoe_logic && slave->proc_preinit != NULL) {
        if (slave->proc_preinit(slave) < 0) {
          goto fail2;
        }
      }
    }

    // stage 2 preinit: process only FSOE logic devices (this depends on initialized fsoeConf data)
    for (slave = master->first_slave; slave != NULL; slave = slave->next) {
      if (slave->is_fsoe_logic && slave->proc_preinit != NULL) {
        if (slave->proc_preinit(slave) < 0) {
          goto fail2;
        }
      }
    }
  }

  return slave_count;

fail2:
  lcec_clear_config();
fail1:
  rtapi_shmem_delete(shmem_id, lcec_comp_id);
fail0:
  return -1;
}

/// @brief Clear configuration.
void lcec_clear_config(void) {
  lcec_master_t *master, *prev_master;
  lcec_slave_t *slave, *prev_slave;

  // iterate all masters
  master = last_master;
  while (master != NULL) {
    prev_master = master->prev;

    // iterate all masters
    slave = master->last_slave;
    while (slave != NULL) {
      prev_slave = slave->prev;

      // cleanup slave
      if (slave->proc_cleanup != NULL) {
        slave->proc_cleanup(slave);
      }

      slave = prev_slave;
    }

#ifdef EC_USPACE_MASTER
    lcec_stop_activator(master);
#endif

    // release master
    if (master->master) {
      ecrt_release_master(master->master);
    }

    master = prev_master;
  }

#ifdef EC_USPACE_MASTER
  if (lcec_uspace_initialized) {
    uid_t user = setfsuid(0);  // ecrt_lib_cleanup() removes the socket in /run
    ecrt_lib_cleanup();
    setfsuid(user);
    if (lcec_uspace_lock_fd >= 0) {
      close(lcec_uspace_lock_fd);  // after the master has closed the interface
      lcec_uspace_lock_fd = -1;
    }
    lcec_uspace_initialized = 0;
  }
#endif
}

#ifdef __KERNEL__
/// @brief Lock LCEC.
static void lcec_request_lock(void *data) {
  lcec_master_t *master = (lcec_master_t *)data;
  rtapi_mutex_get(&master->mutex);
}

/// @brief Unlock LCEC.
static void lcec_release_lock(void *data) {
  lcec_master_t *master = (lcec_master_t *)data;
  rtapi_mutex_give(&master->mutex);
}
#endif

/// @brief Initialize LinuxCNC HAL pins for the master device.
lcec_master_data_t *lcec_init_master_hal(const char *pfx, int global) {
  lcec_master_data_t *hal_data;

  // alloc hal data
  if ((hal_data = LCEC_HAL_ALLOCATE(lcec_master_data_t)) == NULL) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "hal_malloc() for %s failed\n", pfx);
    return NULL;
  }
  memset(hal_data, 0, sizeof(lcec_master_data_t));

  // export pins
  if (lcec_pin_newf_list(hal_data, master_global_pins, pfx) != 0) {
    return NULL;
  }
  if (!global) {
    if (lcec_pin_newf_list(hal_data, master_pins, pfx) != 0) {
      return NULL;
    }
    if (lcec_param_newf_list(hal_data, master_params, pfx) != 0) {
      return NULL;
    }
  }

  return hal_data;
}

/// @brief Initialize generic LinuxCNC HAL pins for a slave
lcec_slave_state_t *lcec_init_slave_state_hal(char *master_name, char *slave_name) {
  lcec_slave_state_t *hal_data;

  // alloc hal data
  if ((hal_data = LCEC_HAL_ALLOCATE(lcec_slave_state_t)) == NULL) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "hal_malloc() for %s.%s.%s failed\n", LCEC_MODULE_NAME, master_name, slave_name);
    return NULL;
  }
  memset(hal_data, 0, sizeof(lcec_slave_state_t));

  // export pins
  if (lcec_pin_newf_list(hal_data, slave_pins, LCEC_MODULE_NAME, master_name, slave_name) != 0) {
    return NULL;
  }

  return hal_data;
}

/// @brief Update HAL pins for the master.
void lcec_update_master_hal(lcec_master_data_t *hal_data, ec_master_state_t *ms) {
  *(hal_data->slaves_responding) = ms->slaves_responding;
  *(hal_data->state_init) = (ms->al_states & 0x01) != 0;
  *(hal_data->state_preop) = (ms->al_states & 0x02) != 0;
  *(hal_data->state_safeop) = (ms->al_states & 0x04) != 0;
  *(hal_data->state_op) = (ms->al_states & 0x08) != 0;
  *(hal_data->link_up) = ms->link_up;
  *(hal_data->all_op) = (ms->al_states == 0x08);
}

/// @brief Update generic HAL pins for a slave.
void lcec_update_slave_state_hal(lcec_slave_state_t *hal_data, ec_slave_config_state_t *ss) {
  *(hal_data->online) = ss->online;
  *(hal_data->operational) = ss->operational;
  *(hal_data->state_init) = (ss->al_state & 0x01) != 0;
  *(hal_data->state_preop) = (ss->al_state & 0x02) != 0;
  *(hal_data->state_safeop) = (ss->al_state & 0x04) != 0;
  *(hal_data->state_op) = (ss->al_state & 0x08) != 0;
#ifdef EC_HAVE_DC_SYNC_FAILED
  *(hal_data->dc_sync_failed) = ss->dc_sync_failed;
#endif
}

/// @brief Update all input pins across all masters and slaves.
void lcec_read_all(void *arg, long period) {
  lcec_master_t *master;

  int dc_sync_failed = 0;

  // initialize global state
  global_ms.slaves_responding = 0;
  global_ms.al_states = 0;
  global_ms.link_up = (first_master != NULL);

  // process slaves
  for (master = first_master; master != NULL; master = master->next) {
    lcec_read_master(master, period);
    dc_sync_failed = dc_sync_failed || master->dc_sync_failed;
  }

  // update global state pins
  lcec_update_master_hal(global_hal_data, &global_ms);
  *(global_hal_data->dc_sync_failed) = dc_sync_failed;
}

/// @brief Update all output pins across all masters and slaves.
void lcec_write_all(void *arg, long period) {
  lcec_master_t *master;

  // process slaves
  for (master = first_master; master != NULL; master = master->next) {
    lcec_write_master(master, period);
  }
}

/// @brief Activate master on first call (delayed activation)
/// This ensures activation happens inside the real-time thread,
/// minimizing the delay between activation and cyclic communication.
static int lcec_activate_master(lcec_master_t *master) {
  struct timeval tv;
  
  uint64_t initial_app_time;
  
  if (__atomic_load_n(&master->activated, __ATOMIC_ACQUIRE)) {
    return 0;  // Already activated
  }
#ifdef EC_USPACE_MASTER
  if (master->activate_requested) {
    return -1;  // The helper thread is activating the master; skip this cycle
  }
#endif
  
  rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "Activating master %s (delayed activation in RT thread)\n", master->name);
  
  // Initialize application time base (now we're in the RT thread context)
  lcec_gettimeofday(&tv);
  master->app_time_base = EC_TIMEVAL2NANO(tv);
  
#ifdef RTAPI_TASK_PLL_SUPPORT
  master->dc_time_valid_last = 0;
#endif

  // Always use simple wall-clock time formula
  master->app_time_base -= rtapi_get_time();

#ifdef RTAPI_TASK_PLL_SUPPORT
  // Anchor the initial app time to the nominal start of the current servo
  // thread period plus globalShiftTime. IgH places SYNC0 at
  //   app_start_time + n * sync0Cycle + sync0Shift
  // (see fsm_slave_config.c), so SYNC0 fires globalShiftTime + sync0Shift
  // after the nominal start of each servo thread period, independent of
  // wake-up latency and of when lcec's read function happens to run.
  // globalShiftTime is the master's global shift; sync0Shift stays the
  // slave's own (user defined) shift, normally 0.
  // globalShiftTime < appTimePeriod, so the next cycle's app time (taken
  // from "now") is still larger than this one.
  {
    long long ref = rtapi_task_pll_get_reference();
    initial_app_time = master->app_time_base + ((ref > 0) ? ref : rtapi_get_time());
  }
  master->app_time_last = initial_app_time;
  initial_app_time += master->global_shift_time;
  master->app_start_time = initial_app_time;
  master->send_offset_last = 0;
  master->dc_app_offset = 0;
  master->dc_app_offset_valid = 0;
#else
  initial_app_time = master->app_time_base + rtapi_get_time() + master->global_shift_time;
  if (master->sync_to_ref_clock) {
    rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "unable to sync master %s cycle to reference clock, RTAPI_TASK_PLL_SUPPORT not present\n",
        master->name);
  }
#endif

  // Set application time BEFORE activate. The first call sets app_start_time
  // in IgH, which is the anchor for the DC SYNC0 phase calculation.
  ecrt_master_application_time(master->master, initial_app_time);
  rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "Initial app_time set to %llu\n", (unsigned long long)initial_app_time);

#ifdef EC_USPACE_MASTER
  // Hand ecrt_master_activate() to the helper thread (see lcec_activator_main)
  master->activate_requested = 1;
  sem_post(&((lcec_activator_t *)master->activator)->sem);
  return -1;  // skip this cycle
#else
  // Activate master
  {
    long long t0 = rtapi_get_time();
    if (ecrt_master_activate(master->master)) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "failed to activate master %s\n", master->name);
      return -1;
    }
    rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "master %s activated in %lld us\n", master->name, (rtapi_get_time() - t0) / 1000);
  }

  // Get internal process data for domain
  master->process_data = ecrt_domain_data(master->domain);
  master->process_data_len = ecrt_domain_size(master->domain);

  master->activated = 1;
#endif
  rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "Master %s activated successfully\n", master->name);
  
  return 0;
}

/// @brief Read all input pins on a master and its slaves.
void lcec_read_master(void *arg, long period) {
  lcec_master_t *master = (lcec_master_t *)arg;
  lcec_slave_t *slave;
  int check_states;
  int dc_sync_failed;

  // Delayed activation: activate master on first read call
  // Per EtherCAT standard, the cycle starts with receive, so activate here
  // First receive will be empty (no frame sent yet), but that's safe
  if (!master->activated) {
    if (lcec_activate_master(master) != 0) {
      return;  // Activation failed, skip this cycle
    }
  }

  // check period
  if (period != master->period_last) {
    master->period_last = period;
    if (master->app_time_period != period) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "Invalid appTimePeriod of %u for master %s (should be %ld).\n", master->app_time_period,
          master->name, period);
    }
  }

  // get state check flag
  if (master->state_update_timer > 0) {
    check_states = 0;
    master->state_update_timer -= period;
  } else {
    check_states = 1;
    master->state_update_timer = LCEC_STATE_UPDATE_PERIOD;
  }

  // receive process data & master state
  rtapi_mutex_get(&master->mutex);
  ecrt_master_receive(master->master);
  ecrt_domain_process(master->domain);
  if (check_states) {
    ecrt_master_state(master->master, &master->ms);
  }
  rtapi_mutex_give(&master->mutex);

  // update state pins
  lcec_update_master_hal(master->hal_data, &master->ms);

  // update global state
  global_ms.slaves_responding += master->ms.slaves_responding;
  global_ms.al_states |= master->ms.al_states;
  global_ms.link_up = global_ms.link_up && master->ms.link_up;

  // process slaves
  dc_sync_failed = 0;
  for (slave = master->first_slave; slave != NULL; slave = slave->next) {
    // get slaves state
    rtapi_mutex_get(&master->mutex);
    if (check_states) {
      ecrt_slave_config_state(slave->config, &slave->state);
    }
    rtapi_mutex_give(&master->mutex);
    if (check_states) {
      lcec_update_slave_state_hal(slave->hal_state_data, &slave->state);
#ifdef EC_HAVE_DC_SYNC_FAILED
      dc_sync_failed = dc_sync_failed || slave->state.dc_sync_failed;
#endif
    }

    // process read function
    if (slave->proc_read != NULL) {
      slave->proc_read(slave, period);
    }
  }

  // a slave whose DC sync check timed out, as of the last state check
  if (check_states) {
    master->dc_sync_failed = dc_sync_failed;
    *(master->hal_data->dc_sync_failed) = dc_sync_failed;
  }
}

#ifdef RTAPI_TASK_PLL_SUPPORT
/// @brief Update the PLL / DC phase state of a master.
///
/// Called after the frame has been sent. `dc_time` is the (lower 32 bits of
/// the) DC reference clock time sampled when the *previous* cycle's frame
/// passed the reference slave.
///
/// The phase error is the position of the previous cycle's nominal thread
/// start in DC time, relative to the thread grid anchor
/// (app_start_time - globalShiftTime), modulo appTimePeriod and wrapped to
/// +/- half a period. When it is zero, SYNC0 fires globalShiftTime +
/// sync0Shift ns (plus the master's transmit delay) after the nominal start
/// of the servo thread period.
static void lcec_update_dc_phase(lcec_master_t *master, int dc_time_valid, uint32_t dc_time, uint64_t app_time, long long send_offset) {
  lcec_master_data_t *hal_data = master->hal_data;
  int64_t period = master->app_time_period;
  int32_t step;
  int phase_valid = 0;

  *(hal_data->pll_err) = 0;
  *(hal_data->pll_out) = 0;
  *(hal_data->dc_phased) = 0;
  *(hal_data->send_offset) = (int32_t)send_offset;

  // the first read dc_time value seems to be invalid, so wait for two successive successful reads
  if (dc_time_valid && master->dc_time_valid_last && period > 0) {
    // cyclic communication is running; ignore the slow start-up cycles for the max value
    if (*(hal_data->send_offset) > hal_data->send_offset_max) {
      hal_data->send_offset_max = *(hal_data->send_offset);
    }

    // Track the 64-bit offset between DC time and app time by unwrapping the
    // 32-bit DC time. The offset drifts slowly (syncToRefClock=true) or stays
    // nearly constant (syncToRefClock=false), so the per-cycle change always
    // fits in 32 bits. This also makes the initial garbage offset (before IgH
    // has written the slaves' system time offsets) disappear on its own.
    int32_t delta;
    if (!master->dc_app_offset_valid) {
      master->dc_app_offset = (int32_t)(dc_time - (uint32_t)master->app_time_last);
      master->dc_app_offset_valid = 1;
      delta = 0;
    } else {
      delta = (int32_t)(dc_time - (uint32_t)(master->app_time_last + master->dc_app_offset));
      master->dc_app_offset += delta;
    }

    if ((uint32_t)abs(delta) > hal_data->pll_max_err) {
      // DC time jumped (e.g. IgH wrote the system time offset). The offset
      // has been tracked, but skip control this cycle and document the event.
      (*(hal_data->pll_reset_cnt))++;
    } else {
      // DC time at the previous cycle's nominal thread start
      uint64_t dc_wake = master->app_time_last + master->dc_app_offset - master->send_offset_last;
      int64_t phase = (int64_t)(dc_wake - (master->app_start_time - master->global_shift_time)) % period;
      if (phase > period / 2) {
        phase -= period;
      } else if (phase < -(period / 2)) {
        phase += period;
      }
      // positive error: thread starts too early relative to SYNC0, lengthen period
      *(hal_data->pll_err) = (int32_t)(-phase);
      phase_valid = 1;
    }

    if (phase_valid && (uint32_t)abs(*(hal_data->pll_err)) < hal_data->dc_phase_max_err) {
      *(hal_data->dc_phased) = 1;
    }

    // globalShiftTime="auto": a new shift moves the thread against SYNC0
    // through the PLL below, pll-step per cycle, as at start-up
    if (master->auto_shift) {
      master->autoshift.margin = hal_data->auto_shift_margin;
      master->autoshift.input_margin = hal_data->auto_shift_input_margin;
      if (lcec_autoshift_update(&master->autoshift, *(hal_data->dc_phased) && *(hal_data->all_op), *(hal_data->send_offset))) {
        master->global_shift_time = master->autoshift.shift;
        rtapi_print_msg(master->autoshift.fits ? RTAPI_MSG_INFO : RTAPI_MSG_ERR,
            LCEC_MSG_PFX "master %s: send-offset %lld..%lld ns, globalShiftTime %u ns, turn-around %d cycles%s\n", master->name,
            (long long)master->autoshift.send_min, (long long)master->autoshift.send_max, master->global_shift_time,
            master->autoshift.state, master->autoshift.fits ? "" : ", but the send-offset varies too much for it to be the same in every cycle");
      }
      *(hal_data->turnaround_cycles) = master->autoshift.state >= LCEC_AUTOSHIFT_TWO ? master->autoshift.state : 0;
      *(hal_data->shift_time) = master->global_shift_time;
    }

    // syncToRefClock=true: DC is the clock source; steer the servo thread so
    // that its nominal start keeps a fixed phase to SYNC0.
    // syncToRefClock=false: the DC follows the master clock and SYNC0 is
    // already anchored to the thread grid via app_start_time; pll_err is
    // only calculated for monitoring.
    if (master->sync_to_ref_clock && phase_valid) {
      step = (int32_t)hal_data->pll_step;
      *(hal_data->pll_out) = (*(hal_data->pll_err) < 0) ? -step : step;
    }
  }

  // Only masters that steer the thread touch the thread's PLL correction, so
  // that a non-synchronizing master doesn't cancel another master's correction.
  if (master->sync_to_ref_clock) {
    rtapi_task_pll_set_correction(*(hal_data->pll_out));
  }
  master->app_time_last = app_time;
  master->send_offset_last = send_offset;
  master->dc_time_valid_last = dc_time_valid;
}
#endif

/// @brief An orderly stop: while lcec.take-down (or lcec.<master>.take-down)
/// is set, every output is zero and the slaves are taken to PREOP, with
/// process data still exchanged in every cycle. Slaves that instead lose
/// their process data in SAFEOP or OP stop with an error (sync manager
/// watchdog, synchronization error), and CiA 402 drives latch a
/// communication fault. Set it before the servo thread stops, and wait until
/// no slave is above PREOP (the state-safeop and state-op pins):
/// lcec-take-down does both, from [HAL]SHUTDOWN. Clearing it takes the
/// slaves back to OP. Taking the slaves to PREOP needs the userspace master
/// (EC_HAVE_REQUEST_AL_STATE); otherwise only the outputs are zero.
static void lcec_take_down(lcec_master_t *master) {
  int take = *(global_hal_data->take_down) || *(master->hal_data->take_down);

  if (take != master->taking_down) {
    master->taking_down = take;
#ifdef EC_HAVE_REQUEST_AL_STATE
    ecrt_master_request_al_state(master->master, take ? EC_AL_STATE_PREOP : EC_AL_STATE_OP);
    rtapi_print_msg(RTAPI_MSG_INFO, LCEC_MSG_PFX "master %s: %s\n", master->name,
        take ? "take-down: outputs zero, slaves to PREOP" : "take-down cleared: slaves to OP");
#else
    if (take) {
      rtapi_print_msg(RTAPI_MSG_ERR, LCEC_MSG_PFX "master %s: take-down: outputs zero; this master cannot take the slaves to PREOP\n",
          master->name);
    }
#endif
  }
  if (take && master->process_data != NULL) {
    memset(master->process_data, 0, master->process_data_len);
  }
}

/// @brief Write all output pins on a master and its slaves.
void lcec_write_master(void *arg, long period) {
  lcec_master_t *master = (lcec_master_t *)arg;
  lcec_slave_t *slave;
  uint64_t app_time;
  long long now;
#ifdef RTAPI_TASK_PLL_SUPPORT
  long long ref;
  uint32_t dc_time;
  int dc_time_valid;
#endif

  // Skip if master not yet activated (should have been activated in read)
  if (!__atomic_load_n(&master->activated, __ATOMIC_ACQUIRE)) {
    return;
  }

  // process slaves
  for (slave = master->first_slave; slave != NULL; slave = slave->next) {
    if (slave->proc_write != NULL) {
      slave->proc_write(slave, period);
    }
  }

#ifdef RTAPI_TASK_PLL_SUPPORT
  // nominal start time of the current servo thread period
  ref = rtapi_task_pll_get_reference();
#endif

  lcec_take_down(master);

  // send process data
  rtapi_mutex_get(&master->mutex);
  ecrt_domain_queue(master->domain);

  // update application time
  now = rtapi_get_time();
  app_time = master->app_time_base + now;

  ecrt_master_application_time(master->master, app_time);

  // sync ref clock to master
  if (!master->sync_to_ref_clock) {
    if (master->sync_ref_cnt == 0) {
      master->sync_ref_cnt = master->sync_ref_cycles;
      ecrt_master_sync_reference_clock(master->master);
    }
    master->sync_ref_cnt--;
  }

#ifdef RTAPI_TASK_PLL_SUPPORT
  // Get reference clock time (always needed for DC phase checking, regardless of sync_to_ref_clock mode)
  dc_time = 0;
  dc_time_valid = (ecrt_master_reference_clock_time(master->master, &dc_time) == 0);
#endif

  // sync slaves to ref clock
  ecrt_master_sync_slave_clocks(master->master);

  // send domain data
  ecrt_master_send(master->master);
  rtapi_mutex_give(&master->mutex);

#ifdef RTAPI_TASK_PLL_SUPPORT
  // done after ecrt_master_send() to reduce jitter
  lcec_update_dc_phase(master, dc_time_valid, dc_time, app_time, (ref > 0) ? (now - ref) : 0);
#endif
}


#ifndef __KERNEL__
#define BACKTRACE_SIZE 100

// Print a stack backtrace.  This presumably won't work in kernel
// mode.  It doesn't work *quite* right, in that it doesn't pick up
// symbols from lcec.so, but the offsets are enough to debug many
// problems.  Use `objdump --syms /usr/lib/linuxcnc/modules/lcec.so |
// sort | less` to extract a sorted list of symbols if needed.
static void sigsegv_handler(int sig) {
  struct sigaction act;
  int nptrs, i;
  void *buffer[BACKTRACE_SIZE];
  char **strings;

  fprintf(stderr, "LinuxCNC Ethercat crashed with signal %d\n", sig);

  fprintf(stderr, "-- STACK TRACE START\n");
  nptrs = backtrace(buffer, BACKTRACE_SIZE);
  strings = backtrace_symbols(buffer, nptrs);
  if (strings == NULL) {
    return;
  }

  for (i = 0; i < nptrs; ++i) {
    fprintf(stderr, "%d\t%s\n", i, strings[i]);
  }

  free(strings);

  fprintf(stderr, "-- STACK TRACE END\n");

  /* Restore the default behavior for the traped signal */
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_NODEFER | SA_ONSTACK | SA_RESETHAND;
  act.sa_handler = SIG_DFL;
  sigaction(sig, &act, NULL);
  kill(getpid(), sig);
}
#endif
