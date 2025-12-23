#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

// --- AYARLAR ---
#define SAYFA_BOYUTU_KB 4
#define SAYFA_BOYUTU_BYTE (SAYFA_BOYUTU_KB * 1024) // 4096 Byte
#define OFFSET_BITS 12
#define VPN_BITS 20 
#define SAYFA_TABLOSU_BOYUTU (1 << VPN_BITS)

#define FIZIKSEL_RAM_BOYUTU (64 * 1024) // 64 KB RAM
#define MAX_FRAME_SAYISI (FIZIKSEL_RAM_BOYUTU / SAYFA_BOYUTU_BYTE) // 16 Frame

// --- VERİ YAPILARI ---
typedef struct {
    uint32_t frame_number; 
    bool valid;            
} PageTableEntry;

PageTableEntry page_table[SAYFA_TABLOSU_BOYUTU];
uint8_t FIZIKSEL_RAM[FIZIKSEL_RAM_BOYUTU]; 

// frame_owner[fiziksel_frame_no] = sanal_sayfa_no (VPN)
int32_t frame_owner[MAX_FRAME_SAYISI]; 

// [YENİ] Sayfa Değiştirme Algoritması için Değişkenler
int fifo_ptr = 0;       // Sıradaki kurbanı gösterir
int dolu_frame_sayisi = 0; 

// Bellek Pointerları
#define HEAP_BASLANGIC_VPN 10
#define STACK_BASLANGIC_VPN 1000
uint32_t heap_ptr;
uint32_t stack_ptr;

// --- FONKSİYON PROTOTİPLERİ ---
void sistemi_baslat();
void sistemi_sifirla();
void init_heap_stack_maple();
uint32_t fiziksel_cerceve_bul_veya_cal(uint32_t vpn_talep_eden);
void sayfa_maple(uint32_t vpn, uint32_t pfn);
uint32_t adres_cevir(uint32_t sanal_adres);
void stack_push(char veri);
uint8_t stack_pop();
int32_t my_malloc(int boyut);
void my_free(int32_t adres, int boyut);
void show_RAM(int VPN, int size, bool from_end);
void write_data_malloc(int32_t malloc_addr, int offset, uint8_t data);

// --- TEMEL FONKSİYONLAR ---

void sistemi_sifirla() {
    for (int i = 0; i < SAYFA_TABLOSU_BOYUTU; i++) {
        page_table[i].valid = false;
        page_table[i].frame_number = 0;
    }
    // Frame sahipliklerini sıfırla (-1 sahibi yok demek)
    for(int i=0; i < MAX_FRAME_SAYISI; i++) {
        frame_owner[i] = -1;
    }
    
    memset(FIZIKSEL_RAM, 0, FIZIKSEL_RAM_BOYUTU);
    dolu_frame_sayisi = 0;
    fifo_ptr = 0;
    
    printf("Sistem Sifirlandi. RAM: %d KB (%d Frame)\n", FIZIKSEL_RAM_BOYUTU/1024, MAX_FRAME_SAYISI);
}

void sayfa_maple(uint32_t vpn, uint32_t pfn) {
    if (vpn >= SAYFA_TABLOSU_BOYUTU || pfn >= MAX_FRAME_SAYISI) return;

    page_table[vpn].frame_number = pfn;
    page_table[vpn].valid = true;
    
    // Bu çerçevenin sahibi artık bu VPN'dir.
    frame_owner[pfn] = vpn;
    
    printf("  [MAP]: VPN %d -> PFN %d ye eslendi.\n", vpn, pfn);
}

void init_heap_stack_maple() {
    // Heap Başlangıç (2 Sayfa)
    sayfa_maple(10, 0);
    sayfa_maple(11, 1);
    
    // Stack Başlangıç (2 Sayfa)
    sayfa_maple(1000, 2);
    sayfa_maple(999, 3);
    
    dolu_frame_sayisi = 4; // İlk 4 frame dolu
          // Sıradaki işlem 4. frame'den devam etsin
}

void sistemi_baslat() {
    sistemi_sifirla();
    init_heap_stack_maple();
    heap_ptr = 10 << 12; 
    stack_ptr = ((1000 + 1) << 12) - 1;
}


uint32_t fiziksel_cerceve_bul_veya_cal(uint32_t vpn_talep_eden) {
    uint32_t secilen_pfn;

    // 1. Durum: RAM'de hala boş yer var
    if (dolu_frame_sayisi < MAX_FRAME_SAYISI) {
        secilen_pfn = dolu_frame_sayisi; // Sıradaki boşu al
        dolu_frame_sayisi++;
        return secilen_pfn;
    }

    // 2. Durum: RAM Dolu! Birini kovmamız lazım (PAGE REPLACEMENT)
    printf("\n  [UYARI]: RAM Dolu! Sayfa degisimi yapiliyor (Swap-out)...\n");
    
    secilen_pfn = fifo_ptr; 

    // Eski sahibini bul ve kov
    int32_t eski_vpn = frame_owner[secilen_pfn];
    if (eski_vpn != -1) {
        page_table[eski_vpn].valid = false; 
        printf("  [EVICT]: VPN %d RAM'den atildi (Frame %d bosaltildi).\n", eski_vpn, secilen_pfn);
    }

    // FIFO göstergesini güncelle (Döngüsel)
    fifo_ptr = (fifo_ptr + 1) % MAX_FRAME_SAYISI;

    return secilen_pfn;
}

uint32_t adres_cevir(uint32_t sanal_adres) {
    uint32_t vpn = sanal_adres >> OFFSET_BITS;
    uint32_t offset = sanal_adres & ((1 << OFFSET_BITS) - 1);

    if (page_table[vpn].valid) {
        uint32_t pfn = page_table[vpn].frame_number;
        return (pfn << OFFSET_BITS) | offset;
    } else {
        return 0xFFFFFFFF; // Page Fault
    }
}

// --- BELLEK YÖNETİMİ ---

void stack_push(char veri) {
    // ÇAKIŞMA KONTROLÜ
    if (stack_ptr - 1 <= heap_ptr) {
        printf("\n[CRASH]: STACK OVERFLOW! Heap ile cakisti.\n");
        return;
    }

    stack_ptr--;
    uint32_t vpn = stack_ptr >> 12;

    if (page_table[vpn].valid == false) {
        printf("[OS]: Stack icin sayfa lazim (VPN: %d)\n", vpn);
        uint32_t pfn = fiziksel_cerceve_bul_veya_cal(vpn);
        sayfa_maple(vpn, pfn);
    }

    uint32_t fiziksel_adres = adres_cevir(stack_ptr);
    if(fiziksel_adres != 0xFFFFFFFF) FIZIKSEL_RAM[fiziksel_adres] = veri;
}

int32_t my_malloc(int boyut) {
    uint32_t baslangic_adresi = heap_ptr;
    uint32_t yeni_sinir = heap_ptr + boyut;

    // ÇAKIŞMA KONTROLÜ
    if (yeni_sinir >= stack_ptr) {
        printf("\n[CRASH]: HEAP OVERFLOW! Stack alanina girdi. Malloc iptal.\n");
        return -1;
    }

    uint32_t baslangic_vpn = baslangic_adresi >> 12;
    uint32_t bitis_vpn = yeni_sinir >> 12;

    for (uint32_t vpn = baslangic_vpn; vpn <= bitis_vpn; vpn++) {
        if (page_table[vpn].valid == false) {
            printf("[OS]: Heap genisliyor (VPN: %d)\n", vpn);
            
            // [YENİ] A. Özellik burada kullanılıyor: Yer yoksa birini atıp yer açacak
            uint32_t pfn = fiziksel_cerceve_bul_veya_cal(vpn);
            sayfa_maple(vpn, pfn);
        }
    }

    heap_ptr = yeni_sinir;
    return baslangic_adresi;
}


void my_free(int32_t adres, int boyut) {
    if (adres == -1) return;

    uint32_t baslangic_vpn = adres >> 12;
    uint32_t bitis_vpn = (adres + boyut) >> 12;

    printf("\n--- FREE ISLEMI: Adres 0x%X (%d Byte) iade ediliyor ---\n", adres, boyut);

    for (uint32_t vpn = baslangic_vpn; vpn <= bitis_vpn; vpn++) {
        if (page_table[vpn].valid) {
            // Sadece valid bitini kapatıyoruz.
            // Fiziksel çerçeve hala "kirli" ama sistem onu sonra tekrar kullanabilir.
            page_table[vpn].valid = false; 
            printf("  VPN %d serbest birakildi (Invalidate).\n", vpn);
        }
    }
    printf("------------------------------------------------------\n");
}

void show_RAM(int VPN, int size, bool from_end) {
    // Eğer sayfa takas edildiyse (swap-out), göstermeye çalışma
    if (!page_table[VPN].valid) {
        printf("\n[BILGI]: VPN %d su an RAM'de degil (Diske swaplanmis).\n", VPN);
        return;
    }

    uint32_t baslangic_fiziksel;
    if (from_end) baslangic_fiziksel = adres_cevir(((VPN + 1) << 12) - 1);
    else          baslangic_fiziksel = adres_cevir(VPN << 12);

    printf("\n----------------------------------------\n");
    printf(" RAM DOKUMU | Sayfa: %-3d | Boyut: %d\n", VPN, size);
    printf(" Mod: %s | Fiziksel Frame: %d\n", from_end ? "STACK" : "HEAP ", page_table[VPN].frame_number);
    printf("----------------------------------------\n");

    for (int i = 0; i < size; i++) {
        if (i % 10 == 0 && i != 0) printf("\n");
        if (i % 10 == 0) printf(" [%03d]: ", i);

        uint32_t hedef_adres;
        if (from_end) hedef_adres = baslangic_fiziksel - i;
        else          hedef_adres = baslangic_fiziksel + i;

        printf("%02X ", FIZIKSEL_RAM[hedef_adres]);
    }
    printf("\n----------------------------------------\n\n");
}

void write_data_malloc(int32_t malloc_addr, int offset, uint8_t data){
    if (malloc_addr == -1) return;
    
    uint32_t vpn = (malloc_addr + offset) >> 12;
    if (!page_table[vpn].valid) {
        printf("[HATA]: Gecersiz hafizaya erisim! (Segmentation Fault) Adres: 0x%X\n", malloc_addr + offset);
        return;
    }

    uint32_t fiziksel_adres = adres_cevir(malloc_addr + offset);
    FIZIKSEL_RAM[fiziksel_adres] = data;
}

// --- MAIN SENARYOSU ---
int main() {   
    sistemi_baslat();

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

    return 0;
}
