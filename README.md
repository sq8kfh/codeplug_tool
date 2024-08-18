# Motorola Radius R1225 codeplug tool

## About
The tool allows you to decrypt a codeplug file and set the channel frequency from outside the range. The program can work witch Motorola Radius 1225 series RSS archive files.

The tool has been tested with a codeplug archive generated by Motorola Radius 1225 series RSS 3.1 for UHF radio repeater. Probably after changing:
```c
#define IF_FREQ 44850000
#define PLL_PRESCALER 128
```
should work with VHF version of repeater. Currently, only the receiving frequency can be modified.

## Examples

### Decrypt codeplug file:
```bash
codeplug_tool -i codeplug.arc
```
or
```bash
cat codeplug.arc | codeplug_tool > decrypted_codeplug.arc
```

### Print channels list:
```bash
codeplug_tool -p codeplug.arc
```

### Set rx frequency and rx filter to MED:
```bash
codeplug_tool -i -c 1 -r 430000000 -f med codeplug.arc
```
or
```bash
cat codeplug.arc | codeplug_tool -c 1 -r 430000000 -f med > decrypted_codeplug.arc
```
