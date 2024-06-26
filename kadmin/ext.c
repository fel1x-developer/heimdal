/*
 * Copyright (c) 1997 - 2006 Kungliga Tekniska Högskolan
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
 * 3. Neither the name of the Institute nor the names of its contributors
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

#include "kadmin_locl.h"
#include "kadmin-commands.h"

struct ext_keytab_data {
    krb5_keytab keytab;
    int keep;
    int random_key_flag;
    size_t nkstuple;
    krb5_key_salt_tuple *kstuple;
    void *kadm_handle;
};

static int
do_ext_keytab(krb5_principal principal, void *data)
{
    krb5_error_code ret;
    kadm5_principal_ent_rec princ;
    struct ext_keytab_data *e = data;
    krb5_keytab_entry *keys = NULL;
    krb5_keyblock *k = NULL;
    size_t i;
    int n_k = 0;
    uint32_t mask;
    char *unparsed = NULL;

    mask = KADM5_PRINCIPAL;
    if (!e->random_key_flag)
        mask |= KADM5_KVNO | KADM5_KEY_DATA;

    ret = kadm5_get_principal(e->kadm_handle, principal, &princ, mask);
    if (ret)
	return ret;

    ret = krb5_unparse_name(context, principal, &unparsed);
    if (ret)
	goto out;

    if (!e->random_key_flag) {
        if (princ.n_key_data == 0) {
            krb5_warnx(context, "principal has no keys, or user lacks "
                       "get-keys privilege for %s", unparsed);
            goto out;
        }
        /*
         * kadmin clients and servers from master between 1.5 and 1.6
         * can have corrupted a principal's keys in the HDB.  If some
         * are bogus but not all are, then that must have happened.
         *
         * If all keys are bogus then the server may be a pre-1.6,
         * post-1.5 server and the client lacks get-keys privilege, or
         * the keys are corrupted.  We can't tell here.
         */
        if (kadm5_all_keys_are_bogus(princ.n_key_data, princ.key_data)) {
            krb5_warnx(context, "user lacks get-keys privilege for %s",
                       unparsed);
            goto out;
        }
        if (kadm5_some_keys_are_bogus(princ.n_key_data, princ.key_data)) {
            krb5_warnx(context, "some keys for %s are corrupted in the HDB",
                       unparsed);
        }
	keys = calloc(princ.n_key_data, sizeof(*keys));
	if (keys == NULL) {
	    ret = krb5_enomem(context);
	    goto out;
	}
	for (i = 0; i < princ.n_key_data; i++) {
	    krb5_key_data *kd = &princ.key_data[i];

            /* Don't extract bogus keys */
            if (kadm5_all_keys_are_bogus(1, kd))
                continue;

	    keys[i].principal = princ.principal;
	    keys[i].vno = kd->key_data_kvno;
	    keys[i].keyblock.keytype = kd->key_data_type[0];
	    keys[i].keyblock.keyvalue.length = kd->key_data_length[0];
	    keys[i].keyblock.keyvalue.data = kd->key_data_contents[0];
	    keys[i].timestamp = time(NULL);
            n_k++;
	}
    } else if (e->random_key_flag) {
        ret = kadm5_randkey_principal_3(e->kadm_handle, principal, e->keep,
                                        e->nkstuple, e->kstuple, &k, &n_k);
	if (ret)
	    goto out;

	keys = calloc(n_k, sizeof(*keys));
	if (keys == NULL) {
	    ret = krb5_enomem(context);
	    goto out;
	}
	for (i = 0; i < n_k; i++) {
	    keys[i].principal = principal;
	    keys[i].vno = princ.kvno + 1; /* XXX get entry again */
	    keys[i].keyblock = k[i];
	    keys[i].timestamp = time(NULL);
	}
    }

    if (n_k == 0)
        krb5_warn(context, ret, "no keys written to keytab for %s", unparsed);

    for (i = 0; i < n_k; i++) {
	ret = krb5_kt_add_entry(context, e->keytab, &keys[i]);
	if (ret)
	    krb5_warn(context, ret, "krb5_kt_add_entry(%lu)", (unsigned long)i);
    }

  out:
    kadm5_free_principal_ent(e->kadm_handle, &princ);
    if (k) {
        for (i = 0; i < n_k; i++)
            memset(k[i].keyvalue.data, 0, k[i].keyvalue.length);
	free(k);
    }
    free(unparsed);
    free(keys);
    return ret;
}

int
ext_keytab(struct ext_keytab_options *opt, int argc, char **argv)
{
    krb5_error_code ret;
    struct ext_keytab_data data;
    const char *enctypes;
    size_t i;

    data.kadm_handle = NULL;
    ret = kadm5_dup_context(kadm_handle, &data.kadm_handle);
    if (ret)
        krb5_err(context, 1, ret, "Could not duplicate kadmin connection");
    data.random_key_flag = opt->random_key_flag;
    data.keep = 1;
    i = 0;
    if (opt->keepallold_flag) {
        data.keep = 2;
        i++;
    }
    if (opt->keepold_flag) {
        data.keep = 1;
        i++;
    }
    if (opt->pruneall_flag) {
        data.keep = 0;
        i++;
    }
    if (i > 1) {
        fprintf(stderr,
                "use only one of --keepold, --keepallold, or --pruneall\n");
        return EINVAL;
    }

    if (opt->keytab_string == NULL)
	ret = krb5_kt_default(context, &data.keytab);
    else
	ret = krb5_kt_resolve(context, opt->keytab_string, &data.keytab);

    if(ret){
	krb5_warn(context, ret, "krb5_kt_resolve");
	return 1;
    }
    enctypes = opt->enctypes_string;
    if (enctypes == NULL || enctypes[0] == '\0')
        enctypes = krb5_config_get_string(context, NULL, "libdefaults",
                                          "supported_enctypes", NULL);
    if (enctypes == NULL || enctypes[0] == '\0')
        enctypes = "aes128-cts-hmac-sha1-96";
    ret = krb5_string_to_keysalts2(context, enctypes, &data.nkstuple,
                                   &data.kstuple);
    if (ret) {
        fprintf(stderr, "enctype(s) unknown\n");
        krb5_kt_close(context, data.keytab);
        return ret;
    }

    for(i = 0; i < argc; i++) {
	ret = foreach_principal(argv[i], do_ext_keytab, "ext", &data);
	if (ret)
	    break;
    }

    kadm5_destroy(data.kadm_handle);
    krb5_kt_close(context, data.keytab);
    free(data.kstuple);
    return ret != 0;
}
