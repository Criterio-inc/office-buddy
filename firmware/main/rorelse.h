#pragma once

/*
 * Rörelsesensorn QMI8658 på I2C 0x6b. Känner två saker: ett knack på bordet
 * eller kortet (en kort stöt) och ett lyft (en bestående lutningsändring).
 * Båda går vidare till humöret. Anropas en gång efter att I2C är igång.
 */
void rorelse_starta(void);
