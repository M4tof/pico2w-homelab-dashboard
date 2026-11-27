.syntax unified
    .section .rodata
    .balign 4

    .global srv_cert_pem
    .global srv_key_pem

srv_cert_pem:
    .incbin "server.crt"
    .byte 0                  /* Explicit null terminator required by Mbed TLS */

    .balign 4
srv_key_pem:
    .incbin "server.key"
    .byte 0                  /* Explicit null terminator required by Mbed TLS */