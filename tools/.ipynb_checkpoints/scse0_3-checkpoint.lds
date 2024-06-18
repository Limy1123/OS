OUTPUT_ARCH(mips)
/*
Set the architecture to mips.
*/
ENTRY(_start)
/*
Set the ENTRY point of the program to _start.
*/
SECTIONS
{
    /* 设置内核代码段的地址 */
    . = 0x80010000;
    .text : {
        *(.text)
    }

    /* 设置内核数据段的地址 */
    .data : {
        *(.data)
    }

    /* 设置 BSS 段的地址 */
    .bss : {
        *(.bss)
    }


    /* 结束地址 */
    end = .;
}
