# 🧠 Sanal Bellek (Virtual Memory) Simülasyonu – C

Bu proje, **işletim sistemlerinde sanal bellek yönetiminin** temel kavramlarını öğretmek amacıyla C dili ile geliştirilmiş bir **sanal bellek simülasyonudur**.  
Paging, page table, page fault, FIFO page replacement, swap-in / swap-out, heap ve stack yönetimi gibi mekanizmalar yazılımsal olarak modellenmiştir.

---

## 📌 Projenin Amacı

Bu simülasyonun amacı:

- Sanal adres → fiziksel adres çevirimini göstermek
- Page table mantığını öğretmek
- RAM dolduğunda **FIFO sayfa değiştirme algoritmasını** simüle etmek
- Disk (swap alanı) kullanımını göstermek
- Heap (`malloc/free`) ve Stack (push/pop) davranışlarını gözlemlemek
- Page fault, segmentation fault, dirty bit gibi kavramları deneyimlemek

---

## ⚙️ Sistem Özellikleri

| Özellik | Değer |
|------|------|
| Sayfa Boyutu | 4 KB |
| Offset Bit | 12 |
| VPN Bit | 20 |
| Sanal Bellek | ~4 GB (teorik) |
| Fiziksel RAM | 64 KB |
| Frame Sayısı | 16 |
| Sayfa Değiştirme | FIFO |
| Disk (Swap) | 4000 sayfa |

---

## 🧩 Kullanılan Yapılar

### Page Table Entry
```c
typedef struct {
    uint32_t frame_number;
    bool valid;
    bool on_disk;
    bool dirty;
} PageTableEntry;
