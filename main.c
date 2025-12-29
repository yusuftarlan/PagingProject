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
#define DISK_KAPASITESI_VPN 4000 // Simülasyon diski

// --- VERİ YAPILARI ---
typedef struct {
    uint32_t frame_number; 
    bool valid; 
    bool on_disk;  
    bool dirty;         
} PageTableEntry;

PageTableEntry page_table[SAYFA_TABLOSU_BOYUTU];
uint8_t SANAL_DISK[DISK_KAPASITESI_VPN][SAYFA_BOYUTU_BYTE];
uint8_t FIZIKSEL_RAM[FIZIKSEL_RAM_BOYUTU]; 
int32_t frame_owner[MAX_FRAME_SAYISI]; 

// FIFO ve Bellek Durumu
int fifo_ptr = 0;       
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
uint32_t sayfa_eris(uint32_t sanal_adres);
void stack_push(char veri);
uint8_t stack_pop();
int32_t my_malloc(int boyut);
void my_free(int32_t adres, int boyut);
void show_RAM(int VPN, int size, bool from_end);
void show_DISK(int VPN, int size, bool from_end);
void write_data_malloc(int32_t malloc_addr, int offset, uint8_t data);
void swap_out(uint32_t vpn_to_evict, uint32_t pfn);
void swap_in(uint32_t vpn_to_restore, uint32_t new_pfn);
void manuel_ram_kontrol(); // [YENİ]

// --- TEMEL FONKSİYONLAR ---

void sistemi_sifirla() {
    for (int i = 0; i < SAYFA_TABLOSU_BOYUTU; i++) {
        page_table[i].valid = false;
        page_table[i].frame_number = 0;
        page_table[i].on_disk = false;
        page_table[i].dirty = false;
    }
    for(int i=0; i < MAX_FRAME_SAYISI; i++) frame_owner[i] = -1;
    
    memset(FIZIKSEL_RAM, 0, FIZIKSEL_RAM_BOYUTU);
    dolu_frame_sayisi = 0;
    fifo_ptr = 0;
    printf("Sistem Sifirlandi. RAM: %d KB (%d Frame)\n", FIZIKSEL_RAM_BOYUTU/1024, MAX_FRAME_SAYISI);
}

void sayfa_maple(uint32_t vpn, uint32_t pfn) {
    if (vpn >= SAYFA_TABLOSU_BOYUTU || pfn >= MAX_FRAME_SAYISI) return;
    page_table[vpn].frame_number = pfn;
    page_table[vpn].valid = true;
    frame_owner[pfn] = vpn;
    printf("  [MAP]: VPN %d -> PFN %d ye eslendi.\n", vpn, pfn);
}

void init_heap_stack_maple() {
    sayfa_maple(10, 0);   // Heap
    sayfa_maple(11, 1);   // Heap
    sayfa_maple(1000, 2); // Stack
    sayfa_maple(999, 3);  // Stack
    dolu_frame_sayisi = 4; 
}

void sistemi_baslat() {
    sistemi_sifirla();
    init_heap_stack_maple();
    heap_ptr = 10 << 12; 
    stack_ptr = ((1000 + 1) << 12) - 1;
}

uint32_t fiziksel_cerceve_bul_veya_cal(uint32_t vpn_talep_eden) {
    uint32_t secilen_pfn;

    if (dolu_frame_sayisi < MAX_FRAME_SAYISI) {
        secilen_pfn = dolu_frame_sayisi;
        dolu_frame_sayisi++;
        return secilen_pfn;
    }

    printf("\n  [UYARI]: RAM Dolu! Sayfa degisimi yapiliyor (Swap-out)...\n");
    secilen_pfn = fifo_ptr; 
    int32_t eski_vpn = frame_owner[secilen_pfn];

    if (eski_vpn != -1) {
        if (page_table[eski_vpn].on_disk == false) {
            printf("  VPN [%d] diskte degil (Disk guncelleniyor)\n",eski_vpn);
            swap_out(eski_vpn, page_table[eski_vpn].frame_number);
        }
        else if (page_table[eski_vpn].dirty == true){
            printf("  %d nolu sayfa değiştirilmiş!(Disk güncelleniyor)\n",eski_vpn);
            swap_out(eski_vpn, page_table[eski_vpn].frame_number);
        } else {
            printf(" Sayfa temiz (Clean), diske yazma atlandi!\n");
        }
        page_table[eski_vpn].valid = false; 
        page_table[eski_vpn].dirty = false; 
        printf("  [EVICT]: VPN %d RAM'den atildi (Frame %d bosaltildi).\n", eski_vpn, secilen_pfn);
    }
    fifo_ptr = (fifo_ptr + 1) % MAX_FRAME_SAYISI;
    return secilen_pfn;
}

void swap_out(uint32_t vpn_to_evict, uint32_t pfn) {
    uint32_t ram_adres = pfn * SAYFA_BOYUTU_BYTE;
    memcpy(SANAL_DISK[vpn_to_evict], &FIZIKSEL_RAM[ram_adres], SAYFA_BOYUTU_BYTE);
    page_table[vpn_to_evict].valid = false;   
    page_table[vpn_to_evict].on_disk = true;  
    page_table[vpn_to_evict].frame_number = 0; 
    printf("  [SWAP-OUT] VPN %d -> Disk[%d] konumuna yazildi.\n", vpn_to_evict, vpn_to_evict);
}

void swap_in(uint32_t vpn_to_restore, uint32_t new_pfn) {
    uint32_t ram_adres = new_pfn * SAYFA_BOYUTU_BYTE;
    memcpy(&FIZIKSEL_RAM[ram_adres], SANAL_DISK[vpn_to_restore], SAYFA_BOYUTU_BYTE);
    page_table[vpn_to_restore].valid = true;
    page_table[vpn_to_restore].frame_number = new_pfn;
    printf("[SWAP-IN] Disk[%d] -> VPN %d (Frame %d) konumuna yuklendi.\n", vpn_to_restore, vpn_to_restore, new_pfn);
}

uint32_t adres_cevir(uint32_t sanal_adres) {
    uint32_t vpn = sanal_adres >> OFFSET_BITS;
    uint32_t offset = sanal_adres & ((1 << OFFSET_BITS) - 1);
    if (page_table[vpn].valid) {
        uint32_t pfn = page_table[vpn].frame_number;
        return (pfn << OFFSET_BITS) | offset;
    } else return 0xFFFFFFFF;
}

uint32_t sayfa_eris(uint32_t sanal_adres) {
    uint32_t vpn = sanal_adres >> OFFSET_BITS;
    uint32_t fiziksel = adres_cevir(sanal_adres);
    
    if (fiziksel != 0xFFFFFFFF) return fiziksel; 
    
    if (page_table[vpn].on_disk) {
        printf("  [PAGE FAULT] VPN %d diskte, yukleniyor...\n", vpn);
        uint32_t yeni_pfn = fiziksel_cerceve_bul_veya_cal(vpn);
        swap_in(vpn, yeni_pfn);      
        return adres_cevir(sanal_adres);
    } else {
        printf("  [SEGFAULT] VPN %d hic tahsis edilmemis!\n", vpn);
        return 0xFFFFFFFF;
    }
}

// --- BELLEK YÖNETİMİ ---
void stack_push(char veri) {
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
    page_table[vpn].dirty = true;
    uint32_t fiziksel_adres = sayfa_eris(stack_ptr);
    if(fiziksel_adres != 0xFFFFFFFF) {
        FIZIKSEL_RAM[fiziksel_adres] = veri;
        printf("  Push: '%c' (Sanal: 0x%X)\n", veri, stack_ptr);
    }
}

uint8_t stack_pop(){
    uint32_t ust_sinir = ((STACK_BASLANGIC_VPN + 1) << 12) - 1;
    if (stack_ptr >= ust_sinir) {
        printf("\n[HATA]: Stack bos! Pop yapilamaz.\n");
        return 0;
    }
    uint32_t fiziksel_adres = sayfa_eris(stack_ptr);
    uint8_t veri = FIZIKSEL_RAM[fiziksel_adres];
    printf("  Pop : '%c' (Sanal: 0x%X)\n", veri, stack_ptr);
    stack_ptr++;
    return veri;
}

int32_t my_malloc(int boyut) {
    uint32_t baslangic_adresi = heap_ptr;
    uint32_t yeni_sinir = heap_ptr + boyut;
    if (yeni_sinir >= stack_ptr) {
        printf("\n[CRASH]: HEAP OVERFLOW! Stack alanina girdi.\n");
        return -1;
    }
    uint32_t baslangic_vpn = baslangic_adresi >> 12;
    uint32_t bitis_vpn = yeni_sinir >> 12;
    for (uint32_t vpn = baslangic_vpn; vpn <= bitis_vpn; vpn++) {
        if (page_table[vpn].valid == false) {
            printf("[OS]: Heap genisliyor (VPN: %d)\n", vpn);
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
            page_table[vpn].valid = false; 
            printf("  VPN %d serbest birakildi (Invalidate).\n", vpn);
        }
    }
    printf("------------------------------------------------------\n");
}

void show_RAM(int VPN, int size, bool from_end) {
    if (!page_table[VPN].valid) {
        printf("\n[BILGI]: VPN %d su an RAM'de degil (Valid=0).\n", VPN);
        return;
    }
    uint32_t baslangic_fiziksel;
    if (from_end) baslangic_fiziksel = sayfa_eris(((VPN + 1) << 12) - 1);
    else          baslangic_fiziksel = sayfa_eris(VPN << 12);

    printf("\n----------------------------------------\n");
    printf(" RAM DOKUMU | Sayfa: %-3d | Mod: %s\n", VPN, from_end ? "STACK" : "HEAP ");
    printf(" Fiziksel Frame: %d\n", page_table[VPN].frame_number);
    printf("----------------------------------------\n");
    for (int i = 0; i < size; i++) {
        if (i % 10 == 0 && i != 0) printf("\n");
        if (i % 10 == 0) printf(" [%03d]: ", i);
        uint32_t hedef = from_end ? baslangic_fiziksel - i : baslangic_fiziksel + i;
        printf("%02X ", FIZIKSEL_RAM[hedef]);
    }
    printf("\n----------------------------------------\n");
}

void show_DISK(int VPN, int size, bool from_end){
    printf("\n--------------------------------------\n");
    printf(" DISK DOKUMU | Sayfa: %-3d \n", VPN);
    printf("----------------------------------------\n");
    for (int i = 0; i < size; i++) {
        if (i % 10 == 0 && i != 0) printf("\n");
        if (i % 10 == 0) printf(" [%03d]: ", i);
        printf("%02X ", SANAL_DISK[VPN][i]);
    }
    printf("\n----------------------------------------\n");
}

void write_data_malloc(int32_t malloc_addr, int offset, uint8_t data){
    if (malloc_addr == -1) return;
    uint32_t vpn = (malloc_addr + offset) >> 12;
    if (!page_table[vpn].valid) {
        printf("[HATA]: Gecersiz hafizaya erisim! (Segmentation Fault) Adres: 0x%X\n", malloc_addr + offset);
        return;
    }
    page_table[vpn].dirty = true;
    uint32_t fiziksel_adres = sayfa_eris(malloc_addr + offset);
    FIZIKSEL_RAM[fiziksel_adres] = data;
}

// ================= SENARYOLAR =================

void senaryo1() {
    sistemi_baslat();
    printf("\n=== SENARYO 1: TEMEL MALLOC VE YAZMA TESTI ===\n");
    int32_t x =  my_malloc(3);
    int32_t y = my_malloc(3);
    write_data_malloc(x, 0, 15);
    write_data_malloc(x, 1, 16);
    write_data_malloc(y, 0, 32);
    show_RAM(10, 10, 0);
}

void senaryo2() {
    sistemi_baslat();
    printf("\n=== SENARYO 2: RAM DOLDURMA VE SWAP-OUT (FIFO) ===\n");
    int32_t pointers[20];
    for(int i=0; i<14; i++) {
        printf("\n>> Malloc %d. sayfa istiyor...\n", i+1);
        pointers[i] = my_malloc(4000); 
    }
    write_data_malloc(pointers[0], 0, 64);
    printf("\n>> RAM DOLU. YENI MALLOC ISTEGI...\n");
    int32_t extra = my_malloc(4000); 

    printf("\n>> TEST: Kovulan VPN 10 RAM'de mi?\n");
    show_RAM(10, 5, 0); 
    
    uint32_t yeni_vpn = extra >> 12;
    uint32_t sayfa_basi = yeni_vpn << 12;
    write_data_malloc(sayfa_basi, 0, 15);
    show_RAM(yeni_vpn, 5, 0);
    show_DISK(10,20,0);
}

void senaryo3() {
    sistemi_baslat();
    printf("\n=== SENARYO 3: SWAP-IN TESTI (GERI YUKLEME) ===\n");
    int32_t pointers[20];
    for(int i = 0; i < 14; i++) pointers[i] = my_malloc(4000); 
    
    write_data_malloc(pointers[0], 0, 0xAA);
    write_data_malloc(pointers[0], 1, 0xBB);
    show_RAM(10, 5, 0);
    
    printf("\n>> VPN 10'u Diske Gonderiyoruz (Swap-Out)...\n");
    my_malloc(4000); // Bu işlem VPN 10'u kovar
    
    show_RAM(10, 5, 0); 
    show_DISK(10, 5, 0);
    
    printf("\n>> VPN 10'a Tekrar Erisim (Swap-In)...\n");
    uint32_t sanal = 10 << 12;
    if (sayfa_eris(sanal) != 0xFFFFFFFF) {
        printf(">> BASARILI! VPN 10 Geri Geldi.\n");
        show_RAM(10, 5, 0);
    }
}

void senaryo4() {
    sistemi_baslat();
    printf("\n=== SENARYO 4: STACK VE FREE ISLEMLERI ===\n");

    printf("\n--- ADIM 1: STACK TESTI (LIFO) ---\n");
    stack_push('A');
    stack_push('B');
    stack_push('C');
    show_RAM(1000, 5, true); 

    printf("\n>> Stack Pop Islemleri:\n");
    stack_pop(); 
    stack_pop(); 
    
    printf("\n--- ADIM 2: FREE (BELLEK IADESI) TESTI ---\n");
    int32_t ptr = my_malloc(100);
    uint32_t vpn = ptr >> 12;
    
    write_data_malloc(ptr, 0, 0x99);
    printf(">> Veri yazildi (0x99). RAM Durumu:\n");
    show_RAM(vpn, 5, 0);

    my_free(ptr, 100);
    printf("\n>> TEST: Free edilen alana yazma denemesi...\n");
    write_data_malloc(ptr, 0, 0x88);

    printf("\n>> TEST: Ayni yeri tekrar Malloc ile aliyoruz...\n");
    page_table[vpn].valid = true; 
    page_table[vpn].frame_number = fiziksel_cerceve_bul_veya_cal(vpn);
    printf("   [MANUEL]: VPN %d tekrar aktif edildi.\n", vpn);

    write_data_malloc(ptr, 0, 0x77);
    printf(">> Yeni veri yazildi (0x77). RAM Durumu:\n");
    show_RAM(vpn, 5, 0);
}

// [YENİ] MANUEL RAM KONTROL FONKSİYONU
void manuel_ram_kontrol() {
    int vpn, size, mode_secim;
    
    printf("\n--- MANUEL RAM GORUNTULEME ARACI ---\n");
    printf("NOT: En son calistirdiginiz senaryonun durumunu gorursunuz.\n");
    printf("     Eger sistem sifirlandiysa sadece bos RAM gorursunuz.\n\n");
    
    printf("Goruntulemek istediginiz VPN (Sayfa No): ");
    if (scanf("%d", &vpn) != 1) return;
    
    printf("Kac Byte gorunsun (Orn: 10, 50, 100): ");
    if (scanf("%d", &size) != 1) return;

    printf("Okuma Modu (0: Normal/Heap, 1: Tersten/Stack): ");
    if (scanf("%d", &mode_secim) != 1) return;

    // Kullanıcının girdiği bilgilere göre RAM'i göster
    show_RAM(vpn, size, (bool)mode_secim);
}

void menu() {
    int secim = 0;
    while(1) {
        printf("\n============================================\n");
        printf("      SANAL BELLEK SIMULASYONU (OS)         \n");
        printf("============================================\n");
        printf(" 1. Senaryo 1: Basit Malloc ve RAM Yazma\n");
        printf(" 2. Senaryo 2: RAM Doldurma ve Swap-Out (FIFO)\n");
        printf(" 3. Senaryo 3: Swap-In (Diskten Geri Yukleme)\n");
        printf(" 4. Senaryo 4: Stack ve Free (Bellek Iadesi)\n");
        printf(" 5. Manuel RAM Kontrolu (VPN ile Gozat)\n");
        printf(" 6. Cikis\n");
        printf("--------------------------------------------\n");
        printf(" Seciminiz [1-6]: ");
        
        if (scanf("%d", &secim) != 1) {
            while(getchar() != '\n'); 
            secim = 0;
        }

        switch(secim) {
            case 1: senaryo1(); break;
            case 2: senaryo2(); break;
            case 3: senaryo3(); break;
            case 4: senaryo4(); break;
            case 5: manuel_ram_kontrol(); break; // Yeni Seçenek
            case 6: printf("Simulasyon sonlandiriliyor...\n"); return;
            default: printf("\n[HATA]: Gecersiz secim! Lutfen tekrar deneyin.\n");
        }
        
        printf("\n>> Ana menuye donmek icin Enter'a basin...");
        getchar(); getchar(); 
    }
}

int main() {   
    menu();
    return 0;
}
