#ifndef MACHINE_HPP
#define MACHINE_HPP

// Minimalny interfejs maszyny.
// Oryginalny plik był dostarczony w treści zadania i nie jest częścią tego repozytorium.

#include <exception>
#include <memory>

class Product {
public:
    virtual ~Product() = default;
};

class MachineFailure : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override {
        return "Machine failure";
    }
};

class Machine {
public:
    virtual ~Machine() = default;

    // Wytwarza produkt; może rzucić MachineFailure.
    virtual std::unique_ptr<Product> getProduct() = 0;

    // Przyjmuje z powrotem produkt, którego klient nie odebrał.
    virtual void returnProduct(std::unique_ptr<Product> product) = 0;

    virtual void start() = 0;
    virtual void stop() = 0;
};

#endif // MACHINE_HPP
