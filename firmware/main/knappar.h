#pragma once

/*
 * Knapparna på sidan: BOOT (GPIO0) sänker volymen, strömknappen (via
 * AXP2101) höjer den. Varje steg hörs som ett blipp och visas som en rad
 * under ansiktet. Nivån sparas i flashminnet och överlever omstart.
 */
void knappar_starta(void);
