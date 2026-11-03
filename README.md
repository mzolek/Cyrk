# Cyrk – współbieżny system realizacji zamówień

Wielowątkowy system w C++ symulujący restaurację z automatami. Klienci składają zamówienia
i otrzymują pager, wątki pracowników kompletują zamówienia, a wątki maszyn współbieżnie
wytwarzają produkty. System obsługuje awarie maszyn, limit czasu na odbiór zamówienia
oraz kontrolowane zamykanie.

Projekt zaliczeniowy z przedmiotu *Programowanie współbieżne* (Wydział MIM, Uniwersytet Warszawski).

## Jak to działa

- **Klient** wywołuje `System::order()`, dostaje `CoasterPager` i czeka na nim (`wait()`, opcjonalnie z limitem czasu) aż zamówienie będzie gotowe, po czym odbiera je przez `collectOrder()`.
- **Pracownik** (osobny wątek) pobiera zamówienie z kolejki, ustawia się w kolejkach do potrzebnych maszyn i czeka, aż wszystkie produkty zostaną wytworzone.
- **Maszyna** (osobny wątek) obsługuje swoją kolejkę pracowników i przekazuje im produkty.

### Synchronizacja

- Wspólny stan chroniony jednym mutexem; każdy wątek ma **własną zmienną warunkową**, dzięki czemu budzony jest dokładnie ten wątek, którego dotyczy zdarzenie, a nie wszystkie naraz.
- Uśpione maszyny są budzone **potokowo**: każda obudzona maszyna budzi następną z kolejki.
- Obsłużone są wyścigi między stronami, np. maszyny mogą wyprodukować wszystko, zanim pracownik zacznie czekać, a klient może odebrać zamówienie, zanim pracownik zacznie na niego czekać.

### Sytuacje wyjątkowe

- **Awaria maszyny:** zamówienie kończy się błędem (`FulfillmentFailure`), produkt znika z menu, a oczekujące zamówienia z tym produktem są usuwane.
- **Nieodebrane zamówienie:** po upływie limitu czasu produkty wracają do maszyn, a odbiór kończy się `OrderExpiredException`.
- **Zamknięcie (`shutdown()`):** system przestaje przyjmować zamówienia, kończy rozpoczętą pracę, zatrzymuje wszystkie wątki bez zakleszczeń i zwraca raporty pracowników (zamówienia zrealizowane, porzucone i nieudane).

## Pliki

| Plik | Opis |
|---|---|
| `system.hpp` | Implementacja systemu (`System`, `CoasterPager`) |
| `machine.hpp` | Interfejs maszyny i produktu. Oryginalny plik był dostarczony w treści zadania; ta wersja to minimalny interfejs na potrzeby budowania i testów |
| `tests/system_tests.cpp` | Testy jednostkowe (GoogleTest) |
| `CMakeLists.txt` | Konfiguracja budowania (CMake, C++20) |
| `conanfile.txt` | Opcjonalne zależności przez Conana |

## Budowanie i testy

Wymagania: CMake 3.16+, kompilator z obsługą C++20 (np. GCC 10+).

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

GoogleTest jest pobierany automatycznie (FetchContent), chyba że jest już dostępny w systemie.

### Z Conanem

```bash
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=20
cmake --preset conan-debug
cmake --build --preset conan-debug
ctest --preset conan-debug --output-on-failure
```

### Valgrind

```bash
valgrind --leak-check=full --error-exitcode=1 ./build/cyrk_tests
```

### ThreadSanitizer

```bash
cmake -S . -B build-tsan -DCYRK_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

Testy, Valgrind i ThreadSanitizer są uruchamiane automatycznie w GitHub Actions przy każdym pushu.
