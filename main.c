#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define SAYFA_BOYUTU_KB 4
#define SAYFA_BOYUTU_BYTE (SAYFA_BOYUTU_KB * 1024) // 4096 Byte

// 4KB = 4096 = 2^12 olduğu için ofset 12 bittir.
#define OFFSET_BITS 12
#define VPN_BITS 20 // 32 (ADRESBİTİ)- 12(OFSET_BIT) = 20 (SAYFA NUMARASINI GÖSTEREN BİT SAYISI)

// Sayfa Tablosu Boyutu: 2^20 adet girdi olabilir.
#define SAYFA_TABLOSU_BOYUTU (1 << VPN_BITS)

// Sayfa Tablosu Girdisi (Page Table Entry - PTE)
// Her bir sanal sayfanın fiziksel bellekte nerede olduğunu tutar.
typedef struct
{
    uint32_t frame_number; // Fiziksel Çerçeve Numarası (PFN)
    bool valid;            // Bu sayfa bellekte yüklü mü? (Valid bit)
} PageTableEntry;

// Sayfa Tablosu (Global olarak tanımlıyoruz, simülasyon belleği)
PageTableEntry page_table[SAYFA_TABLOSU_BOYUTU];

// 64 Kilobayt RAM. Total 16 Sayfa
#define FIZIKSEL_RAM_BOYUTU (64 * 1024)
uint8_t FIZIKSEL_RAM[FIZIKSEL_RAM_BOYUTU];                         // 64KB TOPLAM RAM
#define MAX_FRAME_SAYISI (FIZIKSEL_RAM_BOYUTU / SAYFA_BOYUTU_BYTE) // 16 sayfa
int bos_frame_indis = 0;

#define HEAP_BASLANGIC_VPN 10
#define STACK_BASLANGIC_VPN 1000
uint32_t heap_ptr;
uint32_t stack_ptr;

// --- FONKSİYON PROTOTİPLERİ ---
void sistemi_sifirla();
void init_heap_stack_maple();
void sistemi_baslat();
void sayfa_maple(uint32_t virtual_page_num, uint32_t physical_frame_num);
uint32_t adres_cevir(uint32_t sanal_adres);
void stack_push(char veri);
uint8_t stack_pop();
int32_t my_malloc(int boyut);

// --- FONKSİYONLAR ---
void sistemi_sifirla()
{
    // Sayfaların ilk değerlerini verir
    for (int i = 0; i < SAYFA_TABLOSU_BOYUTU; i++)
    {
        page_table[i].valid = false;
        page_table[i].frame_number = 0;
    }
    printf("Simulasyon baslatildi. Sayfa boyutu: %d KB\n", SAYFA_BOYUTU_KB);
    memset(FIZIKSEL_RAM, 0, FIZIKSEL_RAM_BOYUTU);
    printf("Fiziksel RAM ici sifirlandi \n");
}

void init_heap_stack_maple()
{
    // HEAP BÖLGESİ
    sayfa_maple(10, bos_frame_indis);
    bos_frame_indis++; // Sayacı artırmayı unutma!
    sayfa_maple(11, bos_frame_indis);
    bos_frame_indis++; // Sayacı artırmayı unutma!

    // STACK BÖLGESİ
    sayfa_maple(1000, bos_frame_indis);
    bos_frame_indis++; // Sayacı artırmayı unutma!
    sayfa_maple(999, bos_frame_indis);
    bos_frame_indis++; // Sayacı artırmayı unutma!
}
/**
 * Sayfa tablosunu başlatır. Tüm valid bitleri false yapar.
 */
void sistemi_baslat()
{
    sistemi_sifirla();
    init_heap_stack_maple();
    heap_ptr = 10 << 12;                // Heap ilk heap sanal adresin konumu
    stack_ptr = ((1000 + 1) << 12) - 1; // Stack son stack sanal adresin konumu
}

/**
 * Simülasyon amaçlı: Belirli bir sanal sayfayı fiziksel bir çerçeveye eşler.
 * (Gerçek işletim sisteminde bu, bir program belleğe yüklenirken yapılır)
 */
void sayfa_maple(uint32_t virtual_page_num, uint32_t physical_frame_num)
{
    if (virtual_page_num >= SAYFA_TABLOSU_BOYUTU || physical_frame_num >= MAX_FRAME_SAYISI)
    {
        printf("Hata: Sayfa Numarasi!\n");
        return;
    }
    page_table[virtual_page_num].frame_number = physical_frame_num;
    page_table[virtual_page_num].valid = true;

    printf("Eslestirme: VPN %u -> PFN %u\n", virtual_page_num, physical_frame_num);
}

/**
 * MMU (Memory Management Unit) Simülasyonu
 * Sanal adresi alır, fiziksel adrese dönüştürür.
 */
uint32_t adres_cevir(uint32_t sanal_adres)
{
    // 1. VPN'i elde et: Adresi sağa 12 bit kaydır (üst 20 biti al)
    uint32_t vpn = sanal_adres >> OFFSET_BITS;

    // 2. Offset'i elde et: Adresi 0xFFF ile VE işlemine sok (alt 12 biti al)
    // (1 << 12) - 1 işlemi 0xFFF (4095) değerini üretir.
    uint32_t offset = sanal_adres & ((1 << OFFSET_BITS) - 1);


    // 3. Sayfa tablosunu kontrol et
    if (page_table[vpn].valid)
    {
        uint32_t pfn = page_table[vpn].frame_number;

        // 4. Fiziksel Adresi Hesapla: (PFN << 12) | Offset
        uint32_t fiziksel_adres = (pfn << OFFSET_BITS) | offset;
        return fiziksel_adres;
    }
    else
    {
        printf("PAGE FAULT! (Sayfa Hatasi): Bu sayfa bellekte degil.\n");
        return 0xFFFFFFFF; // Hata kodu
    }
}

void stack_push(char veri)
{
    // 1. Stack aşağı büyür, adresi azaltıyoruz
    stack_ptr--;

    // 2. Hangi sanal sayfaya denk geldiğini bulalım
    uint32_t vpn = stack_ptr >> 12;

    // 3. KONTROL ZAMANI: Bu sayfa var mı?
    if (page_table[vpn].valid == false)
    {
        printf("\n[UYARI]: Stack siniri asildi! (VPN: %d)\n", vpn);
        printf("[OS]: Yeni sayfa tahsis ediliyor...\n");

        // Yeni fiziksel oda ver (maple)
        sayfa_maple(vpn, bos_frame_indis);

        // Bir sonraki boş odaya geç
        (bos_frame_indis)++;
    }

    // 4. Artık sayfanın var olduğundan eminiz, veriyi yazabiliriz.
    // Önce fiziksel adresi bul
    uint32_t fiziksel_adres = adres_cevir(stack_ptr);

    // Sonra RAM dizisine yaz
    FIZIKSEL_RAM[fiziksel_adres] = veri;

    printf("Stack Push: '%c' -> Sanal: 0x%X (VPN %d)\n", veri, stack_ptr, vpn);
}

uint8_t stack_pop()
{
   
    // 1. ALT SINIR KONTROLÜ - Stack boş mu?
    // Başlangıçta stack_ptr = 0x3E8FFF, hiç push yoksa bu değerdedir
    uint32_t ust_sinir = ((STACK_BASLANGIC_VPN + 1) << 12) - 1;
    if (stack_ptr >= ust_sinir)
    {
        printf("\n[HATA]: Stack bos! Pop yapilamaz.\n");
        return 0; // Hata durumunda 0 döndür
    }

    // 2. Önce mevcut stack_ptr'den oku (push sonrası stack_ptr son yazılanı gösteriyor)
    uint32_t fiziksel_adres = adres_cevir(stack_ptr);
    uint8_t veri = FIZIKSEL_RAM[fiziksel_adres];

    printf("Stack Pop: '%c' <- Sanal: 0x%X (VPN %d)\n", veri, stack_ptr, stack_ptr >> 12);

    // 3. Sonra stack'i yukarı çek (adresi artır)
    stack_ptr++;

    return veri;
}

int32_t my_malloc(int boyut)
{
    // 1. Başlangıç adresini kaydet (Kullanıcıya bunu vereceğiz)
    uint32_t baslangic_adresi = heap_ptr;

    // 2. Yeni sınırı hesapla (Heap YUKARI büyür)
    // heap_ptr global olduğu için kalıcı olarak değişecek.
    uint32_t yeni_sinir = heap_ptr + boyut;

    // 3. SAYFA KONTROLÜ (En Önemli Kısım)
    // İstenilen alan birden fazla sayfa sürebilir.
    // Örn: Sayfa 10'un sonundayız, 5000 byte istedik -> Sayfa 11 ve 12'ye taşarız.

    uint32_t baslangic_vpn = baslangic_adresi >> 12;
    uint32_t bitis_vpn = yeni_sinir >> 12;

    // Başlangıç sayfasından bitiş sayfasına kadar (dahil) kontrol et
    for (uint32_t vpn = baslangic_vpn; vpn <= bitis_vpn; vpn++)
    {

        // Eğer bu sayfa tabloda yoksa (valid == false)
        if (page_table[vpn].valid == false)
        {
            printf("\n[UYARI]: Heap alani genisletiliyor! (VPN: %d)\n", vpn);
            printf("[OS]: Yeni sayfa tahsis ediliyor...\n");

            // Yeni bir fiziksel çerçeve (frame) ver
            // (Burada senin yazdığın ram doluluk kontrolünü de ekleyebilirsin)
            sayfa_maple(vpn, bos_frame_indis);

            // Sıradaki boş çerçeveyi güncelle
            (bos_frame_indis)++;
        }
    }
    // 4. Global pointer'ı güncelle (Artık buraya kadar doluyuz)
    heap_ptr = yeni_sinir;

    printf("Malloc(%d) -> Baslangic: 0x%X, Yeni Heap Ucu: 0x%X\n", boyut, baslangic_adresi, heap_ptr);

    // 5. Ayrılan alanın adresini döndür
    return baslangic_adresi;
}


void show_RAM(int VPN, int size, bool from_end) {

    printf("--%d numarali RAM sayfasi(%d adet veri)--", VPN, size);
    
    if (from_end == true) {
        uint32_t fiziksel_adres = adres_cevir(((VPN + 1) << 12) - 1);
        for(int i = 0; i < size; i ++ ){
            if (i % 10 == 0) printf("\n");
            printf("%02X ", FIZIKSEL_RAM[fiziksel_adres - i]);
        }
    } else {
        uint32_t fiziksel_adres = adres_cevir(VPN  << 12);
        for(int i = 0; i < size; i ++ ){
            if (i % 10 == 0) printf("\n");
            printf("%02X ", FIZIKSEL_RAM[fiziksel_adres + i]);
        }
    }
}

void write_data_malloc(int32_t malloc, int offset, uint8_t data){
    
    uint32_t fiziksel_adres = adres_cevir(malloc + offset);

    FIZIKSEL_RAM[fiziksel_adres] = data;
}

// --- MAIN ---
int main()
{   
    sistemi_baslat();
    int32_t x =  my_malloc(3);
    int32_t y = my_malloc(3);
    write_data_malloc(x, 0, 15);
    write_data_malloc(x, 1, 16);
    write_data_malloc(y, 0, 32);
    show_RAM(10, 4*1024, 0);
=======

    printf("\n=== SENARYO 1: RAM DOLDURMA VE SAYFA DEGISIMI (FIFO) ===\n");
 
    int32_t pointers[20];
 
    for(int i=0; i<14; i++) {
        printf("\n>> Malloc %d. sayfa istiyor...\n", i+1);
        pointers[i] = my_malloc(4000); 
    }
    printf("\n>> KRITIK AN: RAM Dolu iken yeni malloc istegi...\n");
    int32_t extra = my_malloc(4000); 

    // Eski VPN 10'a erişmeye çalışalım (Artık yok olmalı)
    printf("\n>> TEST: Kovulan sayfaya (VPN 10) erisim testi:\n");
    show_RAM(10, 5, 0); // "RAM'de değil" demeli.
    
    
    uint32_t yeni_vpn = extra >> 12;
    uint32_t sayfa_basi_adresi = yeni_vpn << 12;
    write_data_malloc(sayfa_basi_adresi, 0, 15);
    show_RAM(yeni_vpn, 5, 0); // Bunu göstermeli.


    printf("\n=== SENARYO 2: FREE (BELLEK IADESI) ===\n");
    
    uint32_t vpn = extra >> 12;
    uint32_t sayfa_basi = vpn << 12;
    my_free(extra, 4000);

    printf("\n>> TEST: Free edilen adrese (%d) veri yazma denemesi...\n", sayfa_basi);
    write_data_malloc(sayfa_basi, 0, 46); 

    printf("\n>> TEST: Free edilen sayfayi (VPN %d) goruntuleme...\n", vpn);
    show_RAM(vpn, 5, 0);

    printf("\n=== SENARYO 3: CAKISMA (COLLISION) ===\n");
    heap_ptr = stack_ptr - 100; 
    
    printf("Heap Ucu: 0x%X, Stack Ucu: 0x%X\n", heap_ptr, stack_ptr);
    
    my_malloc(200); 


    printf("\n=== SENARYO 4: ODAYI GERI KIRALAMA (RE-MAPPING) ===\n");

    uint32_t kurtarilacak_vpn = extra >> 12; 

    printf("[OS]: VPN %d icin tekrar yer aciliyor...\n", kurtarilacak_vpn);

    // 2. Odaya yeni bir fiziksel çerçeve (Frame) bul
    uint32_t yeni_pfn = fiziksel_cerceve_bul_veya_cal(kurtarilacak_vpn);

    sayfa_maple(kurtarilacak_vpn, yeni_pfn);

    printf("\n>> TEST: Odayi geri aldiktan sonra yazma denemesi...\n");
    
    uint32_t sayfanin_basi = kurtarilacak_vpn << 12;
    write_data_malloc(sayfanin_basi, 0, 153);

    show_RAM(kurtarilacak_vpn, 5, 0);

    show_RAM(11, 5, 0);
    show_RAM(12, 5, 0);
>>>>>>> fb82ad6f10a7fce3404635b05b1664a39525acd5

    return 0;
}