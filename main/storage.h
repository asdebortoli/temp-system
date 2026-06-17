/*
 * storage.h — Buffer offline em NVS (PLAN §5, RF08).
 *
 * Chunk A: apenas o stub de inicialização. Ring buffer em NVS (armazenar quando offline,
 * retransmitir ao reconectar) é implementado no Chunk E, usando a partição "buffer".
 */
#ifndef STORAGE_H
#define STORAGE_H

/* Inicializa o armazenamento offline (partição NVS "buffer"). No-op no Chunk A. */
void storage_init(void);

#endif /* STORAGE_H */
