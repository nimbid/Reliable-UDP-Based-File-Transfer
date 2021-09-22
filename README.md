# Reliable-UDP-Based-File-Transfer

A client and server implementation of UDP-based file transfer. The protocol used borrows from [TFTP](https://en.wikipedia.org/wiki/Trivial_File_Transfer_Protocol) and [FTP](https://en.wikipedia.org/wiki/File_Transfer_Protocol) and provides reliable file transfer.

The project is a part of CSCI 5273: Network Systems.

## Reliability
Reliability is implemented using a STOP-N-WAIT protocol that uses per frame acknowledgements. Also included is a re-try mechanism.

## Commands
Five commands have been implemented: 
* GET 
* PUT
* LS
* DELETE
* EXIT

## Instructions
The makefile stores object files in [obj]() and executables in [run](). Run the following command in the directory where you clone this repo to compile the code.
```
make
```

Once the compilation is done, the server and the client executables can be run in the following manner:

### Server
```
filepath/run/uftp_server [Port Number] 
```
*Port Number* must be greater than 5000.

### Client
```
filepath/run/uftp_client [Server IP Address] [Server Port Number]
```

### Running commands on the client

**NOTE**: In the above commands, 'filepath' must be replaced by the path on your system, based on your current directory. This is especially important for the *LS* command as the server lists its current directory based on where it is running (where it was called from).

## Results
The client and server have been tested to reliably transfer files up to 200 MB in a 1% packet loss environment.

### Creating large files for testing
The following command can be run to create an arbitrary file of a size of your choosing.
```
dd if=/dev/zero of=testfile bs=1024 count=102400
```
The variables *bs* and *count* can be modified to create a file size as large or small as you want.

## Authors
* Nimish Bhide

## License
[MIT](https://choosealicense.com/licenses/mit/)