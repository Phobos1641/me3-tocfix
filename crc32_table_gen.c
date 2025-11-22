#include <stdio.h>

void main(void)
{
    unsigned long CRC_POLYNOMIAL = 0x04C11DB7;
    unsigned long CRC_TABLE_SIZE = 256;

    printf("constexpr std::array<uint32_t, %lu> crc_table[%lu] = {\n", CRC_TABLE_SIZE, CRC_TABLE_SIZE);

    for (unsigned long idx = 0; idx < CRC_TABLE_SIZE; idx++)
    {
        unsigned long crc, bitIdx;

        for (crc = idx << 24, bitIdx = 8; bitIdx != 0; bitIdx--)
        {
            crc = ((crc & 0x80000000) == 0x80000000) ? (crc << 1) ^ CRC_POLYNOMIAL : crc << 1;
        }

        printf("0x%08X, ", crc);

        if ((idx + 1) % 8 == 0)
            printf("\n", crc);
    }

    printf("};\n");
}
