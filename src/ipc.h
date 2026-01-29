/*
 * ipc.h - IPC server for dwl
 * See LICENSE file for copyright and license details.
 */
#ifndef IPC_H
#define IPC_H

/* Initialize IPC server (Unix domain socket on Wayland event loop) */
void ipc_init(void);

/* Clean up IPC server (close connections, remove socket) */
void ipc_cleanup(void);

#endif /* IPC_H */
