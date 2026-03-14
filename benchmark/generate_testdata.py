#!/usr/bin/env python3
"""
Generate 100 test JSON files for benchmarking the JSON validators.
  - 80 valid files  (~1-2 MB each)
  - 20 invalid files (~1-2 MB each, with deliberate schema violations)

Usage:
    python3 generate_testdata.py [--count N] [--users N] [--products N] [--orders N]
"""

import argparse
import json
import os
import random
import string
import sys
from datetime import datetime, timedelta

# ── Random data helpers ────────────────────────────────────────────────────

FIRST_NAMES = ["Alice", "Bob", "Charlie", "Diana", "Eve", "Frank", "Grace",
               "Henry", "Isla", "Jack", "Karen", "Leo", "Mia", "Noah", "Olivia",
               "Paul", "Quinn", "Rose", "Sam", "Tina", "Uma", "Victor", "Wendy"]
LAST_NAMES  = ["Smith", "Jones", "Brown", "Wilson", "Taylor", "Davis", "Miller",
               "Anderson", "Thomas", "Jackson", "White", "Harris", "Martin", "Lee"]
CITIES      = ["New York", "London", "Paris", "Berlin", "Toronto", "Sydney",
               "Tokyo", "Madrid", "Rome", "Amsterdam", "Vienna", "Brussels"]
COUNTRIES   = ["US", "GB", "FR", "DE", "CA", "AU", "JP", "ES", "IT", "NL", "AT", "BE"]
STREETS     = ["Main St", "Oak Ave", "Park Blvd", "Lake Dr", "Hill Rd",
               "Maple Lane", "Cedar Way", "Elm Court", "Pine Street", "Birch Road"]
CATEGORIES  = ["electronics", "clothing", "food", "books", "toys",
               "sports", "home", "beauty", "automotive", "other"]
PRODUCT_NAMES = ["Wireless Headphones", "Running Shoes", "Organic Coffee", "Python Programming Book",
                 "LEGO Set", "Yoga Mat", "Smart Watch", "Leather Wallet", "Camping Tent",
                 "Electric Toothbrush", "Gaming Chair", "Stainless Steel Bottle",
                 "Bluetooth Speaker", "Winter Jacket", "Road Bike", "Kitchen Knife Set"]

rng = random.Random()


def rand_hex(n: int) -> str:
    return "".join(rng.choices("abcdef0123456789", k=n))


def rand_date() -> str:
    base  = datetime(2020, 1, 1)
    delta = timedelta(days=rng.randint(0, 1825), hours=rng.randint(0, 23),
                      minutes=rng.randint(0, 59), seconds=rng.randint(0, 59))
    return (base + delta).strftime("%Y-%m-%dT%H:%M:%SZ")


def rand_address() -> dict:
    num = rng.randint(1, 9999)
    return {
        "street":      f"{num} {rng.choice(STREETS)}",
        "city":        rng.choice(CITIES),
        "state":       rng.choice(["CA", "NY", "TX", "FL", "IL", "WA", "ON", "QC"]),
        "country":     rng.choice(COUNTRIES),
        "postal_code": str(rng.randint(10000, 99999)),
    }


def rand_user(uid: int) -> dict:
    return {
        "id":         f"usr_{uid:08x}",
        "email":      f"user{uid}@example.com",
        "first_name": rng.choice(FIRST_NAMES),
        "last_name":  rng.choice(LAST_NAMES),
        "age":        rng.randint(18, 75),
        "address":    rand_address(),
        "roles":      rng.sample(["user", "seller", "moderator"], k=rng.randint(1, 2)),
        "preferences": {
            "newsletter": rng.choice([True, False]),
            "language":   rng.choice(["en", "fr", "de", "es", "ja", "pt"]),
            "currency":   rng.choice(["USD", "EUR", "GBP", "CAD", "AUD"]),
            "theme":      rng.choice(["light", "dark", "auto"]),
        },
        "created_at": rand_date(),
        "status":     rng.choice(["active", "inactive", "pending"]),
    }


def rand_product(pid: int) -> dict:
    prefix  = "".join(rng.choices(string.ascii_uppercase, k=rng.randint(2, 4)))
    num     = rng.randint(1000, 99999)
    suffix  = "".join(rng.choices(string.ascii_uppercase + string.digits, k=rng.randint(2, 4)))
    price   = round(rng.uniform(0.99, 999.99), 2)
    cat     = rng.choice(CATEGORIES)
    tags    = rng.sample(["sale", "new", "popular", "featured", "limited", "eco",
                          "handmade", "imported", "certified", "premium"], k=rng.randint(0, 5))
    return {
        "id":       f"prod_{pid:08x}",
        "sku":      f"{prefix}-{num:05d}-{suffix}",
        "name":     f"{rng.choice(PRODUCT_NAMES)} {pid}",
        "description": (
            f"High-quality {cat} product. "
            f"{''.join(rng.choices(string.ascii_lowercase + ' ', k=rng.randint(20, 200)))}"
        ),
        "price":          price,
        "compare_at_price": round(price * rng.uniform(1.0, 1.5), 2),
        "category":       cat,
        "tags":           list(dict.fromkeys(tags)),  # uniqueItems
        "stock":          rng.randint(0, 5000),
        "weight_kg":      round(rng.uniform(0.05, 50.0), 3),
        "attributes": {
            "color":    rng.choice(["red", "blue", "green", "black", "white"]),
            "material": rng.choice(["plastic", "metal", "wood", "fabric", "glass"]),
            "warranty": rng.randint(1, 5),
            "certified": rng.choice([True, False]),
        },
        "status": rng.choice(["active", "draft", "out_of_stock"]),
    }


def rand_order(oid: int, user_ids: list, product_ids: list) -> dict:
    n_items  = rng.randint(1, 6)
    items    = []
    subtotal = 0.0
    for _ in range(n_items):
        unit_price = round(rng.uniform(0.99, 499.99), 2)
        qty        = rng.randint(1, 10)
        discount   = round(rng.uniform(0, 30), 2)
        subtotal  += unit_price * qty
        items.append({
            "product_id":       rng.choice(product_ids),
            "quantity":         qty,
            "unit_price":       unit_price,
            "discount_percent": discount,
        })
    subtotal      = round(subtotal, 2)
    tax           = round(subtotal * 0.08, 2)
    shipping_cost = round(rng.uniform(0, 25), 2)
    total         = round(subtotal + tax + shipping_cost, 2)
    created       = rand_date()
    return {
        "id":               f"ord_{oid:012x}",
        "user_id":          rng.choice(user_ids),
        "items":            items,
        "subtotal":         subtotal,
        "tax":              tax,
        "shipping_cost":    shipping_cost,
        "total":            max(total, 0.01),
        "status":           rng.choice(["pending", "confirmed", "processing", "shipped", "delivered"]),
        "shipping_address": rand_address(),
        "billing_address":  rand_address(),
        "payment_method":   rng.choice(["credit_card", "debit_card", "paypal", "bank_transfer"]),
        "created_at":       created,
        "updated_at":       rand_date(),
    }


# ── Dataset builder ────────────────────────────────────────────────────────

def build_dataset(n_users: int, n_products: int, n_orders: int, seed: int) -> dict:
    rng.seed(seed)

    users    = [rand_user(i)    for i in range(n_users)]
    products = [rand_product(i) for i in range(n_products)]

    user_ids    = [u["id"] for u in users]
    product_ids = [p["id"] for p in products]

    orders = [rand_order(i, user_ids, product_ids) for i in range(n_orders)]

    return {
        "metadata": {
            "version":        "1.0.0",
            "generated_at":   datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ"),
            "total_users":    n_users,
            "total_products": n_products,
            "total_orders":   n_orders,
            "environment":    "test",
        },
        "users":    users,
        "products": products,
        "orders":   orders,
    }


INVALID_MUTATIONS = [
    lambda d: d["users"].__setitem__(0, {**d["users"][0], "age": 12}),           # age below 18
    lambda d: d["users"].__setitem__(0, {**d["users"][0], "status": "unknown"}), # bad enum
    lambda d: d["users"][0].pop("email", None),                                  # missing required
    lambda d: d["products"].__setitem__(0, {**d["products"][0], "price": -5}),   # price < 0.01
    lambda d: d["products"][0].pop("sku", None),                                 # missing required
    lambda d: d["products"].__setitem__(0, {**d["products"][0], "category": "weapons"}),  # bad enum
    lambda d: d["orders"].__setitem__(0, {**d["orders"][0], "items": []}),       # minItems = 1
    lambda d: d["orders"].__setitem__(0, {**d["orders"][0], "status": "lost"}),  # bad enum
    lambda d: d["orders"][0].pop("payment_method", None),                        # missing required
    lambda d: d["metadata"].__setitem__("version", "not-a-version"),             # bad pattern
]


def corrupt_dataset(dataset: dict, mutation_idx: int) -> dict:
    import copy
    d = copy.deepcopy(dataset)
    INVALID_MUTATIONS[mutation_idx % len(INVALID_MUTATIONS)](d)
    return d


# ── Main ───────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Generate benchmark test data")
    parser.add_argument("--count",    type=int, default=100, help="Total number of files (default: 100)")
    parser.add_argument("--users",    type=int, default=500, help="Users per file (default: 500)")
    parser.add_argument("--products", type=int, default=200, help="Products per file (default: 200)")
    parser.add_argument("--orders",   type=int, default=1500, help="Orders per file (default: 1500)")
    parser.add_argument("--out",      type=str, default="testdata", help="Output directory")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)

    # Remove stale files so the count matches exactly after generation
    for old in os.listdir(args.out):
        if old.endswith(".json"):
            os.remove(os.path.join(args.out, old))

    n_invalid = max(1, args.count // 5)   # 20% invalid
    n_valid   = args.count - n_invalid

    total_size = 0
    print(f"Generating {n_valid} valid + {n_invalid} invalid files into '{args.out}/'")
    print(f"  Per file: {args.users} users, {args.products} products, {args.orders} orders")
    print()

    for i in range(n_valid):
        path    = os.path.join(args.out, f"valid_{i:03d}.json")
        dataset = build_dataset(args.users, args.products, args.orders, seed=i)
        data    = json.dumps(dataset, separators=(",", ":")).encode()
        with open(path, "wb") as f:
            f.write(data)
        total_size += len(data)
        mb = len(data) / 1_000_000
        sys.stdout.write(f"\r  [{i+1:3d}/{n_valid}] valid files — last: {mb:.2f} MB    ")
        sys.stdout.flush()

    print(f"\n  Done. ({n_valid} files)")

    for i in range(n_invalid):
        path    = os.path.join(args.out, f"invalid_{i:03d}.json")
        dataset = build_dataset(args.users, args.products, args.orders, seed=1000 + i)
        dataset = corrupt_dataset(dataset, i)
        data    = json.dumps(dataset, separators=(",", ":")).encode()
        with open(path, "wb") as f:
            f.write(data)
        total_size += len(data)
        sys.stdout.write(f"\r  [{i+1:3d}/{n_invalid}] invalid files    ")
        sys.stdout.flush()

    print(f"\n  Done. ({n_invalid} files)")
    print()
    print(f"Total generated: {args.count} files — {total_size / 1_000_000:.1f} MB")
    print(f"Average file size: {total_size / args.count / 1_000_000:.2f} MB")


if __name__ == "__main__":
    main()
