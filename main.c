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
#define DISK_KAPASITESI_VPN 4000 //2^20 8bitlik sayı için çok büyük 4000 simülasyon için ideal
// --- VERİ YAPILARI ---
typedef struct {
    uint32_t frame_number; 
    bool valid; 
    bool on_disk;  
    bool dirty;         
} PageTableEntry;

PageTableEntry page_table[SAYFA_TABLOSU_BOYUTU];

//Swap edilmiş RAM frameleri SANAL_DISK e kopyalanacak
uint8_t SANAL_DISK[DISK_KAPASITESI_VPN][SAYFA_BOYUTU_BYTE];

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
void swap_out(uint32_t vpn_to_evict, uint32_t pfn);

// --- TEMEL FONKSİYONLAR ---

void sistemi_sifirla() {
    for (int i = 0; i < SAYFA_TABLOSU_BOYUTU; i++) {
        page_table[i].valid = false;
        page_table[i].frame_number = 0;
        page_table[i].on_disk = false;
        page_table[i].dirty = false;
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
        // Sayfanın güncel olup olmadığı kontrolü. Eğer sayfa diskte yoksa veya kirliyse diske yazılır.
        if (page_table[eski_vpn].on_disk == false) {
            printf("  VPN [%d] diskte degil (Disk guncelleniyor)\n",eski_vpn);
            swap_out(eski_vpn, page_table[eski_vpn].frame_number);
        }
        else if (page_table[eski_vpn].dirty == true){
            printf("  %d nolu sayfa değiştirilmiş!(Disk güncelleniyor)\n",eski_vpn);
            swap_out(eski_vpn, page_table[eski_vpn].frame_number);
        }
        else {
            printf(" Sayfa temiz (Clean), diske yazma atlandi!\n");
            }
        }

        page_table[eski_vpn].valid = false; 
        page_table[eski_vpn].dirty = false; 
        printf("  [EVICT]: VPN %d RAM'den atildi (Frame %d bosaltildi).\n", eski_vpn, secilen_pfn);

    // FIFO göstergesini güncelle (Döngüsel)
    fifo_ptr = (fifo_ptr + 1) % MAX_FRAME_SAYISI;

    return secilen_pfn;
}

// Örnek: VPN 15, Frame 3'te duruyor ve atılacak.
void swap_out(uint32_t vpn_to_evict, uint32_t pfn) {
    
    // RAM'deki fiziksel adresi hesapla
    uint32_t ram_adres = pfn * SAYFA_BOYUTU_BYTE;
    
    // KOPYALAMA: RAM[pfn] -> DISK[vpn]
    // Dikkat: Hedef adres doğrudan 'vpn_to_evict' indeksidir!
    memcpy(SANAL_DISK[vpn_to_evict], &FIZIKSEL_RAM[ram_adres], SAYFA_BOYUTU_BYTE);
    
    // Tabloyu güncelle
    page_table[vpn_to_evict].valid = false;   // Artık RAM'de değil
    page_table[vpn_to_evict].on_disk = true;  // Ama diskte güvende
    page_table[vpn_to_evict].frame_number = 0; // PFN bilgisini silebiliriz
    
    printf("  [SWAP-OUT] VPN %d -> Disk[%d] konumuna yazildi.\n", vpn_to_evict, vpn_to_evict);
}

// Örnek: VPN 15'e erişilmek istendi ama valid=0.
void swap_in(uint32_t vpn_to_restore, uint32_t new_pfn) {
    
    // Yeni tahsis edilen RAM adresi
    uint32_t ram_adres = new_pfn * SAYFA_BOYUTU_BYTE;
    
    // KOPYALAMA: DISK[vpn] -> RAM[new_pfn]
    memcpy(&FIZIKSEL_RAM[ram_adres], SANAL_DISK[vpn_to_restore], SAYFA_BOYUTU_BYTE);
    
    // Tabloyu güncelle
    page_table[vpn_to_restore].valid = true;
    page_table[vpn_to_restore].frame_number = new_pfn;
    // on_disk = true kalabilir (Dirty bit mantığı yoksa yedeği dursun)
    
    printf("[SWAP-IN] Disk[%d] -> VPN %d (Frame %d) konumuna yuklendi.\n", vpn_to_restore, vpn_to_restore, new_pfn);
}

uint32_t adres_cevir(uint32_t sanal_adres) {
    uint32_t vpn = sanal_adres >> OFFSET_BITS;
    uint32_t offset = sanal_adres & ((1 << OFFSET_BITS) - 1);

    if (page_table[vpn].valid) {
        uint32_t pfn = page_table[vpn].frame_number;
        return (pfn << OFFSET_BITS) | offset;
    }else {
        0xFFFFFFFF;
    }
}

// Page fault'u ele alan ayrı bir fonksiyon
uint32_t sayfa_eris(uint32_t sanal_adres) {
    uint32_t vpn = sanal_adres >> OFFSET_BITS;
    
    // 1. Önce normal çeviriyi dene
    uint32_t fiziksel = adres_cevir(sanal_adres);
    
    if (fiziksel != 0xFFFFFFFF) {
        return fiziksel; // Başarılı
    }
    
    // 2. Page Fault! Diskte mi kontrol et
    if (page_table[vpn].on_disk) {
        printf("  [PAGE FAULT] VPN %d diskte, yukleniyor...\n", vpn);
        
        // Yeni frame bul (gerekirse swap-out yapar)
        uint32_t yeni_pfn = fiziksel_cerceve_bul_veya_cal(vpn);
        
        // Diskten yükle
        swap_in(vpn, yeni_pfn);     
        // Şimdi tekrar çevir
        return adres_cevir(sanal_adres);
    } else {
        // Ne RAM'de ne diskte - gerçek hata!
        printf("  [SEGFAULT] VPN %d hic tahsis edilmemis!\n", vpn);
        return 0xFFFFFFFF;
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

    page_table[vpn].dirty = true;

    uint32_t fiziksel_adres = sayfa_eris(stack_ptr);
    if(fiziksel_adres != 0xFFFFFFFF) FIZIKSEL_RAM[fiziksel_adres] = veri;
}

uint8_t stack_pop(){
   
    // 1. ALT SINIR KONTROLÜ - Stack boş mu?
    // Başlangıçta stack_ptr = 0x3E8FFF, hiç push yoksa bu değerdedir
    uint32_t ust_sinir = ((STACK_BASLANGIC_VPN + 1) << 12) - 1;
    if (stack_ptr >= ust_sinir)
    {
        printf("\n[HATA]: Stack bos! Pop yapilamaz.\n");
        return 0; // Hata durumunda 0 döndür
    }

    // 2. Önce mevcut stack_ptr'den oku (push sonrası stack_ptr son yazılanı gösteriyor)
    uint32_t fiziksel_adres = sayfa_eris(stack_ptr);
    uint8_t veri = FIZIKSEL_RAM[fiziksel_adres];

    printf("Stack Pop: '%c' <- Sanal: 0x%X (VPN %d)\n", veri, stack_ptr, stack_ptr >> 12);

    // 3. Sonra stack'i yukarı çek (adresi artır)
    stack_ptr++;

    return veri;
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

void show_DISK(int VPN, int size, bool from_end){

    printf("\n--------------------------------------\n");
    printf(" DISK DOKUMU | Sayfa: %-3d | Boyut: %d\n", VPN, size);
    printf(" Mod: %s", from_end ? "STACK" : "HEAP |\n");
    printf("----------------------------------------\n");

    for (int i = 0; i < size; i++) {
        if (i % 10 == 0 && i != 0) printf("\n");
        if (i % 10 == 0) printf(" [%03d]: ", i);
        printf("%02X ", SANAL_DISK[VPN][i]);
    }
    printf("\n----------------------------------------\n\n");
}


void show_RAM(int VPN, int size, bool from_end) {
    // Eğer sayfa takas edildiyse (swap-out), göstermeye çalışma
    if (!page_table[VPN].valid) {
        printf("\n[BILGI]: VPN %d su an RAM'de degil (Diske swaplanmis).\n", VPN);
        return;
    }

    uint32_t baslangic_fiziksel;
    if (from_end) baslangic_fiziksel = sayfa_eris(((VPN + 1) << 12) - 1);
    else          baslangic_fiziksel = sayfa_eris(VPN << 12);

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
    page_table[vpn].dirty = true;
    uint32_t fiziksel_adres = sayfa_eris(malloc_addr + offset);
    FIZIKSEL_RAM[fiziksel_adres] = data;
}


void senaryo1() { //MALLOC TAHİSİSİ TESTTİ
    sistemi_baslat();
    printf("\n=== SENARYO 1: HEAP BOLGESINDEN 2 MALLOC TAHSISI ===\n");
    int32_t x =  my_malloc(3);
    int32_t y = my_malloc(3);
    write_data_malloc(x, 0, 15);
    write_data_malloc(x, 1, 16);
    write_data_malloc(y, 0, 32);
    show_RAM(10, 50, 0);
}

void senaryo2() { //swap-out testi
    sistemi_baslat();
    printf("\n=== SENARYO 2: RAM DOLDURMA VE SAYFA DEGISIMI (FIFO) ===\n");
 
    int32_t pointers[20];
 
    for(int i=0; i<14; i++) {
        printf("\n>> Malloc %d. sayfa istiyor...\n", i+1);
        pointers[i] = my_malloc(4000); 
    }
    write_data_malloc(pointers[0], 0, 64);
    printf("\n>> KRITIK AN: RAM Dolu iken yeni malloc istegi...\n");
    int32_t extra = my_malloc(4000); 

    // Eski VPN 10'a erişmeye çalışalım (Artık yok olmalı)
    printf("\n>> TEST: Kovulan sayfaya (VPN 10) erisim testi:\n");
    show_RAM(10, 5, 0); // "RAM'de değil" demeli.
    
    
    uint32_t yeni_vpn = extra >> 12;
    uint32_t sayfa_basi_adresi = yeni_vpn << 12;
    write_data_malloc(sayfa_basi_adresi, 0, 15);
    show_RAM(yeni_vpn, 5, 0); // Bunu göstermeli.
    show_DISK(10,20,0);
}

void senaryo3() { // swap-in testi
    sistemi_baslat();
    printf("\n=== SENARYO 3: SWAP-IN TESTI ===\n");
    
    int32_t pointers[20];
    
    // 1. RAM'i doldur (16 frame var, 4'ü heap/stack için kullanıldı, 12 tane daha ekle)
    for(int i = 0; i < 14; i++) {
        printf("\n>> Malloc %d. sayfa istiyor...\n", i + 1);
        pointers[i] = my_malloc(4000); 
    }
    
    // 2. İlk sayfaya (VPN 10) veri yaz
    write_data_malloc(pointers[0], 0, 0xAA);
    write_data_malloc(pointers[0], 1, 0xBB);
    write_data_malloc(pointers[0], 2, 0xCC);
    printf("\n>> VPN 10'a veri yazildi: AA BB CC\n");
    show_RAM(10, 10, 0);
    
    // 3. Yeni malloc ile VPN 10'u diske at (swap-out)
    printf("\n>> KRITIK AN: RAM Dolu, yeni malloc VPN 10'u kovacak...\n");
    int32_t extra = my_malloc(4000);
    
    // 4. VPN 10 artık diskte olmalı
    printf("\n>> VPN 10 durumu kontrol ediliyor:\n");
    printf("   valid: %d, on_disk: %d\n", page_table[10].valid, page_table[10].on_disk);
    show_RAM(10, 5, 0);  // "RAM'de değil" demeli
    show_DISK(10, 10, 0); // Diskte AA BB CC görünmeli
    
    // 5. SWAP-IN TESTI: VPN 10'a tekrar erişim
    printf("\n>> SWAP-IN TESTI: VPN 10'a tekrar erisiliyor...\n");
    uint32_t sanal_adres = 10 << 12; // VPN 10'un başlangıç adresi
    
    // sayfa_eris() kullanarak page fault tetikle ve swap-in yap
    uint32_t fiziksel = sayfa_eris(sanal_adres);
    
    if (fiziksel != 0xFFFFFFFF) {
        printf("\n>> BASARILI! VPN 10 tekrar RAM'e yuklendi.\n");
        printf("   Fiziksel adres: 0x%X\n", fiziksel);
        printf("   Okunan veri: 0x%02X (beklenen: 0xAA)\n", FIZIKSEL_RAM[fiziksel]);
        
        // RAM'deki veriyi göster
        show_RAM(10, 10, 0);
    } else {
        printf("\n>> HATA! Swap-in basarisiz.\n");
    }
    
    // 6. Özet
    printf("\n=== SWAP-IN TESTI SONUCU ===\n");
    printf("VPN 10 -> Frame %d (valid: %d)\n", 
           page_table[10].frame_number, page_table[10].valid);
}

// --- MAIN SENARYOSU ---
int main(){
    
    senaryo3();
    return 0;
}
