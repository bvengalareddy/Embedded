#include<stdio.h>
#include<stdlib.h>

int main()
{
	int a[100],n,c,d,pos,swap;

	scanf("%d",&n);

	for(c=0;i<n;i++)
	{
		scanf("%d",&a[c]);
	}
	printf("\n");

	for(c=0;c<n-1;c++)
	{
		pos=c;
		for(d=c+1;d<n;d++)
		{
			if(a[pos]>a[d])
			{
				pos=d;
			}
		}
		if(pos!=c)
		{
			swap=a[c];
			a[c]=a[pos];
			a[pos]=swap;
		}
	}

	printf("sorted data\n");

	for(c=0;c<n;c++)
	{
		printf("%d\t",a[c]);
	}
	printf("\n");

	return 0;
}
