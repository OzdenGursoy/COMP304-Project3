# COMP304-Project3

This is an assignment of COMP304 course in Koç University.
The code is implemented by Özden Gürsoy & Zahit Başyiğit

We implemented both FIFO and LRU with a linked list.
In FIFO, if the node(physical address) is already contained then no insertion is done.
In LRU, if the node is already contained then the node is put at the end whenever the page is addressed.
Using these two modes, we insert and remove from the list to implement page replacement.
We also added PAGE_FRAME 64 to change the number of pages from 256 to 64.
The page replacement is done whenever a page fault occurs (the physical_page is not contained in our data structure)
