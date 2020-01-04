# Boot Process

## First Stage —— BIOS

- `BIOS` loads the first `512` bytes into `0x7c00`，the `512` bytes is the `bootsect.S`，then jump to here，so now `cs` is `0x07c0`，and `ip` is `0`，but it does not make sense。

## Second Stage —— Bootsect

- Move itself from `BOOTSEG = 0x07c0` to `INITSEG=0x9000` use `rep movw` and jump here，so now `cs` is `0x9000`，`ip` is `0`
- Unknow
- Unknow
- Use `int 0x13` interrupt to read `setup`，loads `4` sectors into `0x90200`
- Call `read_it` to load `system`
- Find root device
- jump to `setup.S`



## Third Stage —— Setup

- Fill `0x90000 - 0x90200` with some paras，it overwrites previous `bootsect`。

- Move system part down to `0x0000`，totally move from `0x10000` to `0x90000`，because 

  in `bootsect` system will be stored at `0x10000`，and `0x90000` is `bootsect`，so the system part wll not beyond `0x90000`。

- Set `IDT` and `GDT`

- Enable `A20` with `8042` chip

- Reprogram interrupts

- Set `PE` flag by use `lmsw`，then jump to `system` where `head.s` located



## Fourth Stage —— Head

Now it is in protected mode，so 

- Setup `IDT` and `GDT`
- Check `x87`
- Setup paging
- Go to `main`