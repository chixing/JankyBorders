#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <stdio.h>
#include <string.h>

#define BS_NAME "git.felix.borders"

struct mach_message {
  mach_msg_header_t header;
  mach_msg_size_t msgh_descriptor_count;
  mach_msg_ool_descriptor_t descriptor;
};

static void send_message(mach_port_t port, char* message, uint32_t len) {
  struct mach_message msg = { 0 };
  msg.header.msgh_remote_port = port;
  msg.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND
                                            & MACH_MSGH_BITS_REMOTE_MASK,
                                            0,
                                            0,
                                            MACH_MSGH_BITS_COMPLEX       );
  msg.header.msgh_size = sizeof(struct mach_message);
  msg.msgh_descriptor_count = 1;
  msg.descriptor.address = message;
  msg.descriptor.size = len;
  msg.descriptor.copy = MACH_MSG_VIRTUAL_COPY;
  msg.descriptor.deallocate = false;
  msg.descriptor.type = MACH_MSG_OOL_DESCRIPTOR;

  mach_msg(&msg.header,
           MACH_SEND_MSG,
           sizeof(struct mach_message),
           0,
           MACH_PORT_NULL,
           MACH_MSG_TIMEOUT_NONE,
           MACH_PORT_NULL              );
}

// Packs arguments into the server's format: each argument NUL-terminated,
// followed by one extra NUL.
static uint32_t pack(char* out, size_t cap, int argc, char** argv) {
  uint32_t len = 0;
  for (int i = 0; i < argc; i++) {
    size_t n = strlen(argv[i]);
    if (len + n + 2 > cap) return 0;
    memcpy(out + len, argv[i], n + 1);
    len += n + 1;
  }
  out[len++] = '\0';
  return len;
}

// Usage: borders-msg <args...>   sends one message, like `borders <args...>`
//        borders-msg < lines     sends one message per line of whitespace-separated arguments
// Exits 1 without sending anything if no borders instance is running.
int main(int argc, char** argv) {
  mach_port_t bs_port = 0, port = 0;
  if (task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &bs_port) != KERN_SUCCESS
      || bootstrap_look_up(bs_port, BS_NAME, &port) != KERN_SUCCESS
      || !port) {
    return 1;
  }

  char message[4096];
  if (argc > 1) {
    uint32_t len = pack(message, sizeof(message), argc - 1, argv + 1);
    if (!len) return 1;
    send_message(port, message, len);
    return 0;
  }

  char line[4096];
  while (fgets(line, sizeof(line), stdin)) {
    char* args[64];
    int count = 0;
    for (char* tok = strtok(line, " \t\n"); tok && count < 64; tok = strtok(NULL, " \t\n")) {
      args[count++] = tok;
    }
    if (!count) continue;

    uint32_t len = pack(message, sizeof(message), count, args);
    if (len) send_message(port, message, len);
  }
  return 0;
}
