# Distributed Parking Reservation System

## Objectives

This project implements a distributed parking reservation service using TCP and UDP socket programming in C. Its objectives are to:

- provide a single command-line client for checking parking availability and managing reservations;
- authenticate guests and registered members with different access levels;
- separate authentication, reservation, and pricing responsibilities into independent backend servers;
- coordinate all client and backend communication through one main server;
- support full, partial, and failed reservation requests safely;
- calculate reservation prices and cancellation refunds; and
- demonstrate process management and application-level protocols over TCP and UDP.

## Solution

The system is split into one client, a main coordination server, and three specialized backend servers. The client communicates only with Server M over a persistent TCP connection. Server M forwards requests to the appropriate backend over UDP and returns a consolidated response to the client.

```text
                           UDP :21893
                    +------ Server A ------+
                    |    Authentication     |
                    |                       |
+--------+  TCP     |  UDP :22893           |
| Client | <----> Server M <----> Server R |
+--------+  :25893  |  :24893   Reservations|
                    |                       |
                    |  UDP :23893           |
                    +------ Server P -------+
                           Pricing/refunds
```

### Components

| Component | Responsibility |
| --- | --- |
| `client.c` | Authenticates a user and provides the interactive `search`, `reserve`, `lookup`, `cancel`, and `quit` commands. |
| `serverM.c` | Accepts TCP client connections, forks a child process per client, routes requests to the backend servers, and combines their responses. |
| `serverA.c` | Authenticates guests and registered members against `members.txt`. |
| `serverR.c` | Loads parking-space state from `spaces.txt` and processes availability, reservation, lookup, confirmation, and cancellation requests. |
| `serverP.c` | Calculates total reservation prices and cancellation refunds. |

### Access model

- Guests can search for available parking only.
- Members can search, reserve, look up their reservations, and cancel reserved time slots.
- The built-in guest credentials are `guest` / `123456`.
- Member passwords are transformed by Server M with a Caesar-style `+3` shift for letters and digits before Server A compares them with the stored value.

### Reservation flow

1. The client sends credentials to Server M over TCP.
2. Server M asks Server A to authenticate the user over UDP.
3. Search requests are forwarded to Server R, optionally filtered to `UPC` or `HSC`.
4. For a reservation, Server R checks all requested slots:
   - if every slot is available, it reserves them;
   - if only some are available, the client is asked whether to reserve the remaining slots;
   - if none are available, the request fails.
5. After a successful reservation, Server M asks Server P to calculate the user's total cost.
6. Cancellation succeeds only when every requested slot belongs to that member. Server P then calculates the refund.

## Networking

All processes bind to `127.0.0.1`.

| Process | Protocol | Port |
| --- | --- | ---: |
| Server A | UDP | 21893 |
| Server R | UDP | 22893 |
| Server P | UDP | 23893 |
| Server M backend socket | UDP | 24893 |
| Server M client socket | TCP | 25893 |

## Requirements

- Linux or another POSIX-compatible environment
- GCC
- GNU Make

The implementation uses POSIX sockets and `fork()`. It is therefore not intended to compile natively on Windows without a compatible environment such as Docker, WSL, or a Linux virtual machine.

## Runtime data files

Two input files in the project root provide sample members and parking-space state. You can edit these files to create different test scenarios.

### `members.txt`

Each line contains a numeric user ID, username, and already-encrypted password:

```text
<user_id> <username> <encrypted_password>
```

Example structure:

```text
1001 alice sdvvzrug
```

Passwords must use the same `+3` transformation implemented by `encrypt_password()` in `serverM.c`: letters wrap within their alphabet, digits wrap from `9` to `0`, and special characters remain unchanged.

The included sample accounts are:

| Username | Plain-text password | User ID |
| --- | --- | ---: |
| `alice` | `password` | 1001 |
| `bob` | `secret123` | 1002 |
| `carol` | `parking9` | 1003 |
| `david` | `Trojan2026` | 1004 |

These credentials are demonstration data only and should not be used for a production system.

### `spaces.txt`

Each line contains a four-character space ID followed by 12 integer slot states:

```text
<space_id> <slot_1> <slot_2> ... <slot_12>
```

Example structure:

```text
U101 0 0 0 0 0 0 0 0 0 0 0 0
H201 0 0 0 0 0 0 0 0 0 0 0 0
```

Space IDs beginning with `U` belong to UPC; IDs beginning with `H` belong to HSC. A slot value of `0` means available, while a nonzero value represents the member ID holding that reservation.

> Reservation changes are maintained in memory and are not written back to `spaces.txt`; restarting Server R reloads the original file state.

## Build

From the project root, run:

```bash
make
```

This creates executables both in the project root and under `exec/`:

```text
client  serverM  serverA  serverR  serverP
```

Other build targets:

```bash
make clean    # remove generated objects and executables
make rebuild  # clean and rebuild everything
```

## Run

Open separate terminals in the project root and start the backend servers before Server M:

```bash
./serverA
```

```bash
./serverR
```

```bash
./serverP
```

```bash
./serverM
```

Finally, start a client with a username and password:

```bash
./client <username> <password>
```

Guest example:

```bash
./client guest 123456
```

## Client commands

Commands are case-insensitive. Parking space codes must contain `U` or `H` followed by three digits, and time slots must be integers from 1 through 12.

| Command | Available to | Description |
| --- | --- | --- |
| `help` | Everyone | Display the commands available to the current user. |
| `search` | Everyone | Show availability across both parking lots. |
| `search UPC` | Everyone | Show available UPC spaces and time slots. |
| `search HSC` | Everyone | Show available HSC spaces and time slots. |
| `reserve U101 1 2` | Members | Reserve one or more slots for a space. |
| `lookup` | Members | Display the member's current reservations. |
| `cancel U101 1 2` | Members | Cancel slots owned by the member and receive a refund amount. |
| `quit` | Everyone | Close the client session. |

## Pricing rules

Each time slot represents two hours. Pricing is calculated across all of a member's current reservations and separately for UPC and HSC.

| Lot | First two hours | Additional hours | Refund per hour |
| --- | ---: | ---: | ---: |
| UPC | $10/hour | $7/hour | $7/hour |
| HSC | $15/hour | $10/hour | $10/hour |

Slots 5 and 9 are treated as peak periods and receive a `1.5x` price multiplier. Refunds use the flat per-hour rates shown above.

## Project structure

```text
.
├── client.c
├── serverM.c
├── serverA.c
├── serverR.c
├── serverP.c
├── members.txt
├── spaces.txt
├── Makefile
├── README.txt
```

## Notes and limitations

- Communication is local-only because all addresses are fixed to `127.0.0.1`.
- Backend communication uses UDP without retry, timeout, or delivery guarantees.
- The reservation state is in-memory and resets whenever Server R restarts.
- Server R stores one pending partial reservation globally, so overlapping partial-reservation flows are not isolated per client.
- Protocol messages use fixed-size buffers and plain-text delimiters, making this an instructional networking project rather than a production service.

## Author

Abhishek Manjunath  
USC ID: 4115238893
