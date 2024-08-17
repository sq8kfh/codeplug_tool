#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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

    fprintf(stderr, "%d\n", iVar2);

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

void print_channels(uint8_t *channel_data) {
    // Base on MB15A02 datasheet and Motorola R1225 service manual

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

        printf("CH alias: %d, RX freq: %u Hz, RX filter: %s\n", ch_alias, rx_freq, rx_filter);
    }
}

int process_data(uint8_t *buf_in, int len, char *output_file, int print_ch)
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

    if (print_ch) {
        print_channels(&buf_out[0x6c]);
        return EXIT_SUCCESS;
    }

    for (int i = 0; i < len; ++i) {
        printf("%c", buf_out[i]);
    }

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    int in_line = 0;
    int channels_list = 0;
    char *channel = NULL;
    char *rx_freq = NULL;
    char *filter = NULL;

    int c;

    opterr = 0;

    while ((c = getopt(argc, argv, "ipc:r:f:")) != -1)
        switch (c) {
          case 'i':
            in_line = 1;
            break;
          case 'p':
            channels_list = 1;
            break;
          case 'c':
            channel = optarg;
            break;
          case 'r':
            rx_freq = optarg;
            break;
          case 'f':
            filter = optarg;
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
            //abort ();
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

            if (process_data(buf_in, len, in_line ? argv[index] : NULL, channels_list) != EXIT_SUCCESS) {
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

        if (process_data(buf_in, len, NULL, channels_list) != EXIT_SUCCESS) {
            fprintf(stderr, "Process data fail\n");
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
