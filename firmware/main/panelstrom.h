#pragma once

/*
 * Panelens ström och reset går via I/O-expandern TCA9554, som BSP:n aldrig
 * rör. Här sätts pinnarna som utgångar och panelen får ett riktigt reset-
 * pulståg. Anropas FÖRE bsp_display_start.
 */
void panelstrom_starta(void);

/* Säkrar att utgångarna fortfarande är utgångar och höga. Billig, körs ofta. */
void panelstrom_sakra(void);
