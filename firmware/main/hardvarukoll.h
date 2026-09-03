/* Frågar kortet vad som sitter på det och skriver svaret i loggen. */
#pragma once
void hardvarukoll_kor(void);

/*
 * Läser av strömkretsen AXP2101 och I/O-expandern TCA9554 och skriver dem
 * som en kort rad i ut. Panelens matning går genom dem, så när glaset
 * slocknar medan chippet kör vidare är det här man ser vad som ändrats.
 */
void hardvarukoll_stromlage(char *ut, size_t storlek);
