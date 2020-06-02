# Sockopt Documentation

## Purpose
This Documentation is for those that are seeking to better understand the
setsockopt and getsockopt functions role in the SSA.

###Admin file
The SSA provides a default configuration file that can later be manipulated by a
system admin, and contains the default configuration settings for running the SSA.
The admin file contains a list of acceptable TLS versions and ciphersuites for use
of the SSA both as a client and as a server.

The user of the SSA uses the configuration file to create an initial context
(SSL_CTX) object, which is then used the to create secure connections (SSL objects)
the user can both observe which connections are being used and set settings required
for their specific connection. For example, if a user wishes to not use an
available cipher, they can do so using a setsockopt() with flag TLS_DISABLE_CIPHER If a user wants
 to know what ciphers can be used, they could use a getsockopt() with flag TLS_TRUSTED_CIPHERS
