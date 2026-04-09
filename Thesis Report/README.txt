Assembled by Jeremy Wong, January 2024.
jermwong@student.ubc.ca

This is a sample thesis project for UBC based off of the original template from Michael McNeil Forbes from 2001. I have added a few updates to it that make it compatible with Faculty of Graduate and Postdoctoral Studies' formatting guidelines. This is a read-only document so you will have to make a duplicate of the project to edit your own version. You may run into compiling issues as this document ages.

Please note that it is ultimately your responsibility to ensure that your thesis conforms to the guidelines published by the Faculty of Graduate and Postdoctoral Studies. 

In particular, the very first line of code:
>> \documentclass[masc,oneside]{ubcthesis}
defines your program. In this case, the program is `masc`: Master of Applied Science. Change this to match your progrma. The options are defined in the `genthesis.cls` file. If your program is not listed here, you will have to edit the genthesis.cls file.
- {phd}   -> Doctor of Philosophy
- {msc}   -> Master of Science
- {masc}  -> Master of Applied Science
- {ma}    -> Master of Arts
- {meng}  -> Master of Engineering

The next important parts to look at are the chapters. There are two types of chapters. The first of these are the admin chapters. These contain files which generate the correct pages in order to satisfy the faculty guidelines. Pages like the committee page and the acknowledgements page live here; edit them but do not delete any of these mandatory folders or files. 

The most relevant ones are your body chapters; they are the main matter of your thesis. These can be self-contained in the `body_chapters` folder. I recommend you keep all figures in a separate `fig` folder within each section. If your thesis is figure-heavy, this will help reduce the compile time; the free version of Overleaf has compile timeouts which can hinder your writing.

`main.tex` contains the structure of the thesis, including most of the administration inputs such as author, title, abstract, etc. To leverage the modularity of latex and stay lightweight, it `includes` all of your chapters. It is the first file you should look at, especially regarding the formatting requirements of the thesis. When adding new chapters, you will have to change this file.

You may have to make edits to the formatting here and there to get what you need for your program. Read the comments directly in the files where applicable. In particular, please check the `committee_page` file where you will have to update your committee.

Do not delete the following files:
- genthesis.cls
- ubcthesis.cls
- ubcthesis.dtx
- ubcthesis.fdb_latexmk
- ubcthesis.ins
Latex is very picky about where class files live relative to the `main.tex` file. I've tried skirting around it but nothing worked. Just avoid touching these files. 