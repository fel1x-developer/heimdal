/*
 * Copyright (c) 1997 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. All advertising materials mentioning features or use of this software 
 *    must display the following acknowledgement: 
 *      This product includes software developed by Kungliga Tekniska 
 *      H�gskolan and its contributors. 
 *
 * 4. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "kdc_locl.h"

RCSID("$Id$");

sig_atomic_t exit_flag = 0;

static RETSIGTYPE
sigterm(int sig)
{
    exit_flag = 1;
}

void
usage(void)
{
    fprintf(stderr, "Usage: %s [-p]\n", __progname);
    exit(1);
}

int
main(int argc, char **argv)
{
    krb5_context context;
    int c;
    set_progname(argv[0]);
    
    configure(argc, argv);

    if(keyfile){
	FILE *f;
	size_t len;
	unsigned char buf[1024];
	EncryptionKey key;
	f = fopen(keyfile, "r");
	if(f == NULL){
	    kdc_log(0, "Failed to open master key file %s", 
		    keyfile);
	    exit(1);
	}
	len = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if(decode_EncryptionKey(buf, len, &key, &len)){
	    kdc_log(0, "Failed to parse contents of master key file %s", 
		    keyfile);
	    exit(1);
	}	    
	set_master_key(&key);
	memset(key.keyvalue.data, 0, key.keyvalue.length);
	free_EncryptionKey(&key);
    }else{
	des_cblock key;
	des_new_random_key(&key);
	memset(&key, 0, sizeof(key));
    }


    signal(SIGINT, sigterm);
    krb5_init_context(&context);
    loop(context);
    krb5_free_context(context);
    return 0;
}
