#!/usr/bin/env python3

import socket

HOST = '127.0.0.1'  # The (receiving) host IP address (sock_host)
PORT = 1717         # The (receiving) host port (sock_port)

# Open the connection (ctrl-c / ctrl-break to quit)
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:		# UDP
#with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:	# TCP
    sock.bind((HOST, PORT))		# UDP
#    sock.connect((HOST, PORT))	# TCP
    
    data = ""
    prevdir = 0
    chgsum = 0
    # Keep receiving data until FicTrac closes
    while True:
        # Receive one data frame
        new_data = sock.recv(1024)
        if not new_data:
            break
        
        # Decode received data
        data += new_data.decode('UTF-8')
        
        # Find the first frame of data
        endline = data.find("\n")
        line = data[:endline]       # copy first frame
        data = data[endline+1:]     # delete first frame
        
        # Tokenise
        toks = line.split(", ")
        
		# Check that we have sensible tokens
        if ((len(toks) < 24) | (toks[0] != "FT")):
            print('Bad read')
            continue
        
        # Extract FicTrac variables
        # (see https://github.com/rjdmoore/fictrac/blob/master/doc/data_header.txt for descriptions)
        cnt = int(toks[1])
        dr_cam = [float(toks[2]), float(toks[3]), float(toks[4])]
        err = float(toks[5])
        dr_lab = [float(toks[6]), float(toks[7]), float(toks[8])]
        r_cam = [float(toks[9]), float(toks[10]), float(toks[11])]
        r_lab = [float(toks[12]), float(toks[13]), float(toks[14])]
        posx = float(toks[15])
        posy = float(toks[16])
        heading = float(toks[17])
        step_dir = float(toks[18])
        step_mag = float(toks[19])
        intx = float(toks[20])
        inty = float(toks[21])
        ts = float(toks[22])
        seq = int(toks[23])
        
        # Do something ...
        #print(cnt)
        chgsum += (heading-prevdir)
        prevdir = heading
        print(f'X: {heading}')
        if cnt % 10 == 0:
            print(f'direction: {chgsum}')
            chgsum = 0
