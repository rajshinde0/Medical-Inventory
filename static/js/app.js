/* ==========================================================================
   Shared front-end behaviour.
   Previously there was no static/js/ at all: every page inlined its own
   <script> block and duplicated the same select2 setup.
   ========================================================================== */
(function () {
    "use strict";

    var THEME_KEY = "mims-theme";

    /* ---------------------------------------------------------------------
       Theme toggle. base.html already applied the stored theme before first
       paint; this only wires up the button and keeps the icon in sync.
       --------------------------------------------------------------------- */
    function currentTheme() {
        return document.documentElement.getAttribute("data-bs-theme") || "light";
    }

    function syncThemeIcon() {
        var dark = currentTheme() === "dark";
        document.querySelectorAll("[data-theme-icon]").forEach(function (icon) {
            icon.classList.toggle("fa-moon", !dark);
            icon.classList.toggle("fa-sun", dark);
        });
        var toggle = document.getElementById("themeToggle");
        if (toggle) {
            toggle.setAttribute(
                "aria-label",
                dark ? "Switch to light theme" : "Switch to dark theme"
            );
        }
    }

    function initTheme() {
        var toggle = document.getElementById("themeToggle");
        syncThemeIcon();
        if (!toggle) {
            return;
        }
        toggle.addEventListener("click", function () {
            var next = currentTheme() === "dark" ? "light" : "dark";
            document.documentElement.setAttribute("data-bs-theme", next);
            try {
                localStorage.setItem(THEME_KEY, next);
            } catch (e) {
                /* private mode / storage disabled -- theme just won't persist */
            }
            syncThemeIcon();
        });
    }

    /* ---------------------------------------------------------------------
       Select2 on any [data-select2] element, with the Bootstrap 5 theme.
       --------------------------------------------------------------------- */
    function initSelect2() {
        if (typeof window.jQuery === "undefined" || !window.jQuery.fn.select2) {
            return;
        }
        window.jQuery("[data-select2]").each(function () {
            var $el = window.jQuery(this);
            $el.select2({
                theme: "bootstrap-5",
                width: "100%",
                placeholder: $el.data("placeholder") || "Search...",
                allowClear: false
            });
        });
    }

    /* ---------------------------------------------------------------------
       Medicine picker. Shared by buy / update / restock, which each had their
       own near-identical copy of this logic.

       Two bugs are fixed here:
       1. The expired check used `$opt.data('expired') === 'true'`. jQuery's
          .data() coerces the attribute string "true" into the boolean true,
          so that strict comparison was ALWAYS false -- the expired warning
          never appeared and the new-expiry field was never made required.
       2. buy.html bound its quantity 'input' handler INSIDE the select's
          change handler, so selecting N medicines attached N handlers, each
          closing over a stale price.
       --------------------------------------------------------------------- */
    function toBool(value) {
        return value === true || value === "true";
    }

    function formatMoney(value) {
        return "₹" + Number(value).toFixed(2);
    }

    function initMedicinePicker() {
        var picker = document.getElementById("medicineSelect");
        if (!picker) {
            return;
        }

        var panel = document.getElementById("medicineInfo");
        var qtyInput = document.getElementById("qty");
        var expiryInput = document.getElementById("expiry");
        var expiredWarning = document.getElementById("expiredWarning");
        var totalBox = document.getElementById("totalCost");
        var totalValue = document.getElementById("totalValue");
        var maxQtyLabel = document.getElementById("maxQty");
        var mode = picker.getAttribute("data-mode") || "";

        var state = { price: 0, stock: 0, expired: false };

        function readSelection() {
            var opt = picker.options[picker.selectedIndex];
            if (!opt || !opt.value) {
                return null;
            }
            return {
                name: opt.getAttribute("data-name") || "",
                batch: opt.value,
                price: parseFloat(opt.getAttribute("data-price") || "0"),
                stock: parseInt(opt.getAttribute("data-stock") || "0", 10),
                expiry: opt.getAttribute("data-expiry") || "",
                expired: toBool(opt.getAttribute("data-expired"))
            };
        }

        function setText(id, value) {
            var el = document.getElementById(id);
            if (el) {
                el.textContent = value;
            }
        }

        function updateTotal() {
            if (!totalBox || !qtyInput) {
                return;
            }
            var qty = parseInt(qtyInput.value, 10);
            if (!qty || qty < 1 || !state.price) {
                totalBox.classList.add("d-none");
                return;
            }
            totalBox.classList.remove("d-none");
            if (totalValue) {
                totalValue.textContent = formatMoney(qty * state.price);
            }
        }

        picker.addEventListener("change", function () {
            var med = readSelection();

            if (!med) {
                if (panel) { panel.classList.remove("show"); }
                if (totalBox) { totalBox.classList.add("d-none"); }
                state = { price: 0, stock: 0, expired: false };
                return;
            }

            state.price = med.price;
            state.stock = med.stock;
            state.expired = med.expired;

            setText("infoName", med.name);
            setText("infoBatch", med.batch);
            setText("infoPrice", formatMoney(med.price));
            setText("infoStock", med.stock + " units");
            setText("infoExpiry", med.expiry + (med.expired ? "  (expired)" : ""));

            if (panel) {
                panel.classList.add("show");
            }

            if (expiredWarning) {
                expiredWarning.classList.toggle("d-none", !med.expired);
            }

            // Restocking an expired item requires a fresh expiry date. The
            // server enforces this too (app.py restock()).
            if (expiryInput && mode === "restock") {
                expiryInput.required = med.expired;
            }

            if (maxQtyLabel) {
                maxQtyLabel.textContent = med.stock;
            }

            if (qtyInput) {
                if (mode === "buy") {
                    // The markup used to hard-code max="1" while the label
                    // said "Maximum available: 0".
                    qtyInput.max = med.stock > 0 ? med.stock : 1;
                }
                if (mode === "update") {
                    qtyInput.value = med.stock;
                }
                updateTotal();
            }

            if (expiryInput && mode === "update" && med.expiry) {
                // A type="date" input only accepts YYYY-MM-DD; anything else
                // silently blanks the field.
                expiryInput.value = /^\d{4}-\d{2}-\d{2}$/.test(med.expiry)
                    ? med.expiry
                    : "";
            }
        });

        // Bound once, outside the change handler.
        if (qtyInput) {
            qtyInput.addEventListener("input", updateTotal);
        }
    }

    /* ---------------------------------------------------------------------
       Give every date input a sensible floor of today unless the markup
       already set one.
       --------------------------------------------------------------------- */
    function initDateFloors() {
        var today = new Date().toISOString().split("T")[0];
        document.querySelectorAll('input[type="date"][data-min-today]').forEach(
            function (input) {
                if (!input.min) {
                    input.min = today;
                }
            }
        );
    }

    /* ---------------------------------------------------------------------
       Dashboard row filter. The old markup carried
       style="display: table-row !important" on every <tr>, which beat
       jQuery's .hide() and left all four filters silently broken.
       --------------------------------------------------------------------- */
    function initInventoryFilter() {
        var buttons = document.querySelectorAll("[data-filter]");
        var table = document.getElementById("inventoryTable");
        if (!buttons.length || !table) {
            return;
        }

        var rows = table.querySelectorAll("tbody tr[data-stock]");
        var counter = document.getElementById("filterCount");

        function apply(filter) {
            var shown = 0;
            rows.forEach(function (row) {
                var expired = toBool(row.getAttribute("data-expired"));
                var stock = parseInt(row.getAttribute("data-stock") || "0", 10);
                var visible = true;

                if (filter === "active") {
                    visible = !expired && stock > 0;
                } else if (filter === "expired") {
                    visible = expired;
                } else if (filter === "out") {
                    visible = stock === 0;
                }

                row.hidden = !visible;
                if (visible) {
                    shown += 1;
                }
            });
            if (counter) {
                counter.textContent = shown;
            }
        }

        buttons.forEach(function (button) {
            button.addEventListener("change", function () {
                apply(button.getAttribute("data-filter"));
            });
            button.addEventListener("click", function () {
                apply(button.getAttribute("data-filter"));
            });
        });

        apply("all");
    }

    /* ---------------------------------------------------------------------
       Enable a submit button only once a confirmation checkbox is ticked.
       Used by the restore page. The button is NOT disabled in the markup --
       four forms used to ship disabled submit buttons that only JS could
       enable, making them unusable if a script failed to load.
       --------------------------------------------------------------------- */
    function initConfirmGates() {
        document.querySelectorAll("[data-confirm-gate]").forEach(function (box) {
            var target = document.getElementById(
                box.getAttribute("data-confirm-gate")
            );
            if (!target) {
                return;
            }
            var sync = function () {
                target.disabled = !box.checked;
            };
            box.addEventListener("change", sync);
            sync();
        });
    }

    document.addEventListener("DOMContentLoaded", function () {
        initTheme();
        initSelect2();
        initMedicinePicker();
        initDateFloors();
        initInventoryFilter();
        initConfirmGates();
    });
})();
