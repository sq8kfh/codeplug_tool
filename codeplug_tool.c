#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#define CODEPLUG_SIZE 640
#define IF_FREQ 44850000
#define PLL_PRESCALER 128

int16_t salt1;
int16_t salt2;

unsigned int weird_fun(uint16_t param_1, uint16_t param_2, uint16_t param_3, uint16_t param_4)
{
    if ((param_4 | param_2) == 0) {
        return (param_1 * param_3) & 0xffff;
    }

    unsigned int tmp = (param_1 * param_3) >> 16;
    tmp += (param_2 * param_3) & 0xffff;
    tmp += (param_1 * param_4) & 0xffff;
    return tmp << 16 | ((unsigned int)(param_1 * param_3)  & 0xffff);
}

void init_salt(int16_t param_1)
{
    salt1 = param_1;
    salt2 = 0;
}

unsigned short blend_salt(void)
{
  int lVar1;
  
  lVar1 = weird_fun(salt1, salt2, 0x43fd, 3);
  salt2 = (short)((unsigned int)(lVar1 + 0x269ec3) >> 0x10);
  salt1 = (short)(lVar1 + 0x269ec3);
  return salt2 & 0x7fff;
}

int decrypt(const uint8_t *buf_in, int16_t size, uint8_t *buf_out)
{
  int16_t iVar1;
  int16_t iVar2;
  uint8_t tmp;
  int16_t i;
  
  for (i = 0; i < size; i = i + 1) {
    buf_out[i] = buf_in[i];
  }
  if (buf_out[0x10] != 0) {
    iVar2 = ((uint16_t)buf_out[0x10] - (uint16_t)buf_out[9]) + (uint16_t)buf_out[8];

    init_salt(1);
    for (i = 10; i < size; i = i + 1) {
      tmp = buf_out[i];
      if (i != 0x10) {
        tmp = tmp ^ (uint8_t)iVar2;
        buf_out[i] = tmp;
      }
      iVar1 = blend_salt();
      iVar2 = (int16_t)(iVar1 + (uint16_t)tmp + iVar2) % 0x100;
    }
  }
  return 0;
}

uint8_t calc_header_crc(uint8_t *buf) {
  uint8_t tmp = 0;
  for (int i = 0; i < 0x12; i++) {
    tmp = tmp + buf[i];
  }
  return ~tmp + 1;
}

uint8_t calc_channels_crc(uint8_t *channel_data) {
    uint8_t s = channel_data[0] + channel_data[1] + 0;
    s = ~s + 1;
    s = ~s + 1;

    for (int i = 3; i < channel_data[1] * 16 + 3; ++i) {
        s += channel_data[i];
    }

    return ~s + 1;
}

void print_channels(uint8_t *channel_data) {
    // Base on MB15A02 datasheet and Motorola R1225 service manual

    uint8_t crc =  calc_channels_crc(channel_data);
    printf("Channels list CRC: 0x%x, %s\n", crc, crc != channel_data[2] ? "NOT MATCH!" : "match.");

    for (int c = 0; c < channel_data[1]; ++c) {
        uint8_t *ch_data = &channel_data[c * 16 + 3];

        int ch_alias = ch_data[0];
        uint32_t P = PLL_PRESCALER;
        uint32_t Fosc_div_R;
        switch (ch_data[1] >> 6 & 0x03) {
          case 0:
            Fosc_div_R = 6250;
            break;
          case 1:
            Fosc_div_R = 5000;
            break;
          case 2:
            Fosc_div_R = 3750;
            break;
          case 3:
            Fosc_div_R = 2500;
            break;
        }
        uint32_t N = (uint32_t)(ch_data[1] & 0x07) << 8;
        N |= ch_data[2];
        uint32_t A = (ch_data[3] >> 1) & 0x7f;
        uint32_t rx_freq = Fosc_div_R * (P * N + A) + IF_FREQ;

        char *rx_filter = NULL;
        switch (ch_data[13] & 0x07) {
          case 1:
            rx_filter = "LO";
            break;
          case 2:
            rx_filter = "MED";
            break;
          case 4:
            rx_filter = "HI";
            break;
        }

        printf("CH %d, alias: %d%d, RX freq: %u Hz, RX filter: %s\n", c + 1, ch_alias >> 4 & 0x0f, ch_alias & 0x0f, rx_freq, rx_filter);
    }
}

int set_rx_freq(uint8_t *channel_data, uint8_t ch, uint32_t rx_freq) {
    uint8_t *ch_data = &channel_data[(ch - 1) * 16 + 3];;

    if ((ch - 1) < channel_data[1]) {
        fprintf(stderr, "Invalid channel number %u\n", ch);
        return EXIT_FAILURE;
    }

    uint32_t tmp = rx_freq - IF_FREQ;

    uint8_t Fosc_div_R = 0;

    if (tmp % 6250 == 0) {
        Fosc_div_R = 0;
        tmp = tmp / 6250;
    }
    else if (tmp % 5000 == 0) {
        Fosc_div_R = 1;
        tmp = tmp / 5000;
    }
    else if (tmp % 3750 == 0) {
        Fosc_div_R = 2;
        tmp = tmp / 3750;
    }
    else if (tmp % 2500 == 0) {
        Fosc_div_R = 3;
        tmp = tmp / 2500;
    }
    else {
        fprintf(stderr, "Frequency must be an even multiple of 6.25kHz, 5kHz or 3.75kHz.");
        return EXIT_FAILURE;
    }
    uint32_t N = tmp / PLL_PRESCALER;
    uint8_t A = tmp - (N * PLL_PRESCALER);

    ch_data[1] = (Fosc_div_R << 6) | (N >> 8 & 0x07);
    ch_data[2] = N & 0xff;
    ch_data[3] = A << 1;

    return EXIT_SUCCESS;
}

int set_rx_filter(uint8_t *channel_data, uint8_t ch, int filter) {
    uint8_t *ch_data = &channel_data[(ch - 1) * 16 + 3];;

    if ((ch - 1) < channel_data[1]) {
        fprintf(stderr, "Invalid channel number %u\n", ch);
        return EXIT_FAILURE;
    }

    uint8_t f = 0;
    switch (filter) {
        case 1:
            f = 1;
            break;
        case 2:
            f = 2;
            break;
        case 3:
            f = 4;
            break;
    }

    ch_data[13] = (ch_data[13] & 0xf4) | (f & 0x0f);

    return EXIT_SUCCESS;
}

int process_data(uint8_t *buf_in, int len, char *output_file, int print_ch, uint8_t ch_alias, uint32_t rx_freq, int filter)
{
    uint8_t buf_out[1000];

    int i = 0;

    if (buf_in[0x10] != 0) {
        fprintf(stderr, "Encrypted data - decrypting...\n");

        i = CODEPLUG_SIZE;

        decrypt(buf_in, CODEPLUG_SIZE, buf_out);
    }

    for (; i < len; ++i) {
        buf_out[i] = buf_in[i];
    }

    if (buf_out[0x12] != calc_header_crc(buf_out)) {
        fprintf(stderr, "Header CRC mismatch\n");
        return EXIT_FAILURE;
    }

    if (buf_out[0x10] != 0) {
        buf_out[0x10] = 0; //mark as decrypted
        buf_out[0x12] = calc_header_crc(buf_out);
    }

    if (output_file) {
        FILE *fp = fopen(output_file, "w");
        if (!fp) {
            perror("File opening failed");
            return EXIT_FAILURE;
        }
        fwrite(buf_out, sizeof(uint8_t), len, fp);
        fclose(fp);
    }

    if (rx_freq && set_rx_freq(&buf_out[0x6c], ch_alias, rx_freq) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    if (filter && set_rx_filter(&buf_out[0x6c], ch_alias, filter) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    if (rx_freq || filter) {
        buf_out[0x6c + 2] = calc_channels_crc(&buf_out[0x6c]);
    }

    if (print_ch) {
        print_channels(&buf_out[0x6c]);
        return EXIT_SUCCESS;
    }

    for (int i = 0; i < len; ++i) {
        printf("%c", buf_out[i]);
    }

    return EXIT_SUCCESS;
}

void print_help(FILE * stream) {
    fprintf(stream, "Motorola Radius R1225 codeplug tool by Kamil Palkowski.\n");
    fprintf(stream, "The tool allows you to decrypt a codeplug file and set the channel frequency from outside the range.\n");
    fprintf(stream, "The program can work witch Motorola Radius 1225 series RSS archive files.\n");
    fprintf(stream, "\n");

    fprintf(stream, "Usage: codeplug_tool [options] codeplug_archive_file\n");
    fprintf(stream, "       cat codeplug_archive_file | codeplug_tool [options] > decrypted_codeplug_archive_file\n");
    fprintf(stream, "\n");

    fprintf(stream, "Options:\n");
    fprintf(stream, "  -c <num>          Select channel to edit.\n");
    fprintf(stream, "  -f <LO|MED|HI>    Select reception filter\n");
    fprintf(stream, "  -h                Print this message and exit.\n");
    fprintf(stream, "  -i                Edit files in-place.\n");
    fprintf(stream, "  -p                Print channels list and exit.\n");
    fprintf(stream, "  -r rx_freq        Set rx frequency in Hz.\n");
}

int main(int argc, char *argv[])
{
    int in_line = 0;
    int channels_list = 0;
    uint8_t channel_alias = 0;
    uint32_t rx_freq = 0;
    int filter = 0;

    int c;

    opterr = 0;

    while ((c = getopt(argc, argv, "hipc:r:f:")) != -1)
        switch (c) {
          case 'h':
            print_help(stdout);
            return EXIT_SUCCESS;
          case 'i':
            in_line = 1;
            break;
          case 'p':
            channels_list = 1;
            break;
          case 'c':
            channel_alias = strtol(optarg, (char **)NULL, 10);
            break;
          case 'r':
            rx_freq = strtol(optarg, (char **)NULL, 10);
            break;
          case 'f':
            if (strcasecmp(optarg, "lo") == 0)
                filter = 1;
            else if (strcasecmp(optarg, "med") == 0)
                filter = 2;
            else if (strcasecmp(optarg, "hi") == 0)
                filter = 3;
            else {
                fprintf(stderr, "Option -f requires one of the arguments: LO | MED | HI.\n");
                return EXIT_FAILURE;
            }
            break;
          case '?':
            if (optopt == 'c' || optopt == 'r' || optopt == 'f')
                fprintf(stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint (optopt))
                fprintf(stderr, "Unknown option `-%c'.\n", optopt);
            else
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
            return EXIT_FAILURE;
          default:
            print_help(stderr);
            return EXIT_FAILURE;
        }

    if (optind < argc) {
        for (int index = optind; index < argc; index++) {
            uint8_t buf_in[1000];

            FILE *fp = fopen(argv[index], "r");
            if (!fp) {
                perror("File opening failed");
                return EXIT_FAILURE;
            }
            int len = fread(buf_in, sizeof(uint8_t), 1000, fp);
            fclose(fp);

            if (process_data(buf_in, len, in_line ? argv[index] : NULL, channels_list, channel_alias, rx_freq, filter) != EXIT_SUCCESS) {
                fprintf(stderr, "Process file %s fail\n", argv[index]);
                return EXIT_FAILURE;
            }
        }
    }
    else {
        uint8_t buf_in[1000];

        int len = 0;

        for (unsigned char ch; read(STDIN_FILENO, &ch, 1) > 0; len++) {
            buf_in[len] = ch;
        }

        if (process_data(buf_in, len, NULL, channels_list, channel_alias, rx_freq, filter) != EXIT_SUCCESS) {
            fprintf(stderr, "Process data fail\n");
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
