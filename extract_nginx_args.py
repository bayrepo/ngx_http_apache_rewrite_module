#!/usr/bin/env python3
"""
Extract nginx configure arguments from `nginx -V` output.
Correctly handles quoted arguments with balanced single quotes.
Returns the parsed command line for use in package_preparer.sh.
"""

import subprocess
import sys


def parse_args(text):
    """
    Разбить строку аргументов на список, корректно обрабатывая одинарные кавычки.

    Пример:
        --with-cc-opt='-O2 -g' --with-ld-opt='...'
    должно разобиться на два элемента, а не на несколько битых строк.
    """
    args = []
    current = ""
    in_single_quotes = False

    for char in text:
        if char == "'":
            # Переходим в режим одинарных кавычек или выходим из него,
            # и добавляем кавычку в текущий аргумент
            in_single_quotes = not in_single_quotes
            current += char
            continue

        if char == " " and not in_single_quotes:
            # Пробел вне кавычек - конец аргумента
            if current.strip():
                args.append(current.strip())
                current = ""
        else:
            # Добавляем символ в текущий аргумент
            current += char

    # Последний аргумент, если остался
    if current.strip():
        args.append(current.strip())

    return args


def main(nginx_src_dir="."):
    """
    Запустить nginx -V и извлечь аргументы конфигурации.

    Args:
        nginx_src_dir: каталог с исходниками nginx (обычно ./nginx-VER)

    Returns:
        0 на успех, 1 на ошибку. Вывод аргументов в stderr для использования bash скриптом.
    """
    # Запускаем nginx -V через PATH, вывод идёт в stderr!
    print(f"Running nginx -V...", file=sys.stderr)

    try:
        result = subprocess.run(["nginx", "-V"], capture_output=True, text=True)
    except FileNotFoundError as e:
        print(f"Error: Could not find 'nginx' in PATH: {e}", file=sys.stderr)
        print("Make sure nginx is installed and added to PATH.", file=sys.stderr)
        return 1

    # Используем stderr вместо stdout!
    output = result.stderr

    if not output:
        print(
            f"Error: empty output from nginx -V. Return code: {result.returncode}",
            file=sys.stderr,
        )
        return 1

    # Ищем строку с configure arguments
    for line in output.split("\n"):
        if "configure arguments:" in line:
            # Извлекаем всё после "configure arguments:"
            config_line = line.split("configure arguments:", 1)[1]

            # Парсим аргументы с учётом кавычек
            args_list = parse_args(config_line)

            print(f"Found configure arguments.", file=sys.stderr)
            print(f"Parsed {len(args_list)} arguments.", file=sys.stderr)
            for arg in args_list:
                print(arg, file=sys.stderr)

            # Удаляем параметры --add-dynamic-module, --with-ld-opt и все --with-*module
            args_to_remove = ["--add-dynamic-module", "--with-ld-opt"]
            filtered_args = []
            for arg in args_list:
                should_skip = False
                for remove_arg in args_to_remove:
                    if arg.startswith(remove_arg):
                        should_skip = True
                        break
                # Проверка на паттерн --with-*module
                if not should_skip and arg.startswith("--with-"):
                    suffix = arg[7:]  # Убираем "--with-"
                    if any(suffix.endswith(m) for m in ["module", "=dynamic"]):
                        should_skip = True
                if not should_skip:
                    filtered_args.append(arg)
            args_list = filtered_args

            # Добавляем наш модуль в конец
            args_list.append("--add-dynamic-module=../modules/mod_rewrite")
            #args_list.append("--with-debug")

            print(f"Added --add-dynamic-module=../modules/mod_rewrite", file=sys.stderr)

            # Формируем команду configure и выводим в stderr
            cmd_line = "./configure " + " ".join(args_list)
            print("=== GENERATED CONFIGURE COMMAND ===", file=sys.stderr)
            print(cmd_line, file=sys.stdout)
            # Execute the generated configure command
            result = subprocess.run(
                cmd_line, shell=True, capture_output=True, text=True
            )
            # Print stdout and stderr of the configure command
            if result.stdout:
                print(result.stdout, file=sys.stdout)
            if result.stderr:
                print(result.stderr, file=sys.stderr)
            if result.returncode != 0:
                print(
                    f"Error: configure command failed with return code {result.returncode}",
                    file=sys.stderr,
                )
                return 1

            return 0

    print("Error: configure arguments line not found in nginx output", file=sys.stderr)
    return 1


if __name__ == "__main__":
    nginx_src_dir = "."

    if len(sys.argv) > 1:
        nginx_src_dir = sys.argv[1]

    exit_code = main(nginx_src_dir)
    sys.exit(exit_code)
